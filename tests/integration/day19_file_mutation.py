import argparse
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

from day16_download_integration import exchange, require


def patch(target, value):
    body = json.dumps(value, ensure_ascii=False).encode("utf-8")
    return exchange("PATCH", target, body)


def delete(target, payload=b""):
    request = (f"DELETE {target} HTTP/1.1\r\nHost: localhost\r\n"
               f"Content-Length: {len(payload)}\r\nConnection: close\r\n\r\n").encode("ascii")
    with socket.create_connection(("127.0.0.1", 8080), timeout=5) as sock:
        sock.sendall(request + payload)
        wire = bytearray()
        while True:
            block = sock.recv(4096)
            if not block:
                break
            wire.extend(block)
            require(len(wire) < 65536, "DELETE response too large")
    head, separator, body = bytes(wire).partition(b"\r\n\r\n")
    require(separator, "missing DELETE header boundary")
    lines = head.decode("ascii").split("\r\n")
    status = int(lines[0].split()[1])
    headers = dict((name.lower(), value.strip())
                   for name, value in (line.split(":", 1) for line in lines[1:]))
    if status == 204:
        require(not body and "content-length" not in headers and
                "transfer-encoding" not in headers, "invalid 204 framing")
    else:
        require(int(headers["content-length"]) == len(body), "DELETE error length")
    return status, headers, body


def run_checks(root):
    ids = []
    payload = b"A\x00B\r\n"
    name = '学习笔记"第二版.txt'
    for _ in range(2):
        status, headers, _ = exchange("POST", "/api/files", payload)
        require(status == 201, "upload failed")
        file_id = headers["x-file-id"]
        require(file_id not in ids, "reused file ID")
        ids.append(file_id)
        status, _, body = patch(f"/api/files/{file_id}", {"name": name})
        require(status == 200 and json.loads(body) ==
                {"id": file_id, "name": name, "size": len(payload)}, "rename response")
        require((root / "data" / "objects" / file_id).read_bytes() == payload,
                "rename moved object or changed bytes")
    print("PASS: two objects accept the same UTF-8 name without changing IDs or bytes")

    target = f"/api/files/{ids[0]}"
    for value in ({}, {"name": 1}, {"name": "x", "extra": 1}, [],
                  *({"name": value} for value in
                    ("", "..", "../escape", "bad\\name", "bad\r\nname", " " * 3,
                     "x" * 256, "中" * 86))):
        require(patch(target, value)[0] == 400, "invalid name or schema accepted")
    for body in (b"{", b'{"name":"\xff"}'):
        require(exchange("PATCH", target, body)[0] == 400, "invalid JSON or UTF-8 accepted")
    require(json.loads(exchange("GET", target)[2])["name"] == name, "failed patch changed name")
    print("PASS: invalid JSON, schema and names leave the old name intact")

    files = json.loads(exchange("GET", "/api/files")[2])["files"]
    require({item["id"] for item in files} == set(ids) and
            all(item["name"] == name for item in files), "list rename visibility")
    for file_id in ids:
        path = f"/api/files/{file_id}"
        status, get_headers, body = exchange("GET", path)
        head_status, head_headers, head_body = exchange("HEAD", path)
        require(status == head_status == 200 and get_headers == head_headers and not head_body,
                "renamed detail HEAD")
        status, headers, body = exchange("GET", path + "/content")
        require(status == 200 and body == payload and headers.get("content-disposition") ==
                f'attachment; filename="{file_id}.bin"', "download after rename")
    print("PASS: list and detail show the name while downloads retain content and safe filenames")

    status, headers, _ = patch("/api/files", {"name": "x"})
    require(status == 405 and headers.get("allow") == "GET, HEAD, POST", "list method policy")
    require(delete(target + "/content")[0] == 405, "download DELETE method")
    require(patch("/health", {"name": "x"})[0] == 405, "health PATCH method")
    require(exchange("PUT", target)[0] == 501, "unknown method policy")
    require(delete(target, b"X")[0] == 400, "DELETE body accepted")
    require(exchange("GET", target)[0] == 200, "failed DELETE removed object")
    print("PASS: known method errors are 405 and invalid DELETE leaves the object intact")

    require(delete(target)[0] == 204, "delete failed")
    require(not (root / "data" / "objects" / ids[0]).exists(), "deleted disk name remains")
    for method in ("GET", "HEAD"):
        require(exchange(method, target)[0] == 404, "deleted detail remained visible")
        require(exchange(method, target + "/content")[0] == 404, "deleted content remained visible")
    require(delete(target)[0] == 404 and patch(target, {"name": "x"})[0] == 404,
            "deleted object mutation policy")
    files = json.loads(exchange("GET", "/api/files")[2])["files"]
    require(len(files) == 1 and files[0]["id"] == ids[1], "deleted object remained listed")
    print("PASS: 204 has no body or length header and new requests see the object as missing")
    return ids, payload, name


def run_server(server, root, checks):
    log_path = root / "server.log"
    with log_path.open("wb") as log:
        process = subprocess.Popen([str(server)], cwd=root, stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 5
            while True:
                require(process.poll() is None, "test server exited before listening")
                if "listening on 127.0.0.1:8080" in log_path.read_text(
                        encoding="utf-8", errors="replace"):
                    break
                require(time.monotonic() < deadline, "test server did not start")
                time.sleep(0.01)
            return checks()
        except Exception:
            print(log_path.read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
            raise
        finally:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: real NetDisk mutation integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    with tempfile.TemporaryDirectory(prefix="netdisk-day19-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        ids, payload, name = run_server(server, root, lambda: run_checks(root))

        def after_restart():
            survivor = f"/api/files/{ids[1]}"
            status, _, body = exchange("GET", survivor)
            require(status == 200 and json.loads(body)["name"] == name,
                    "persistent name was lost after restart")
            require(exchange("GET", survivor + "/content")[2] == payload, "survivor bytes changed")
            require(exchange("GET", f"/api/files/{ids[0]}/content")[0] == 404,
                    "deleted object returned after restart")
        run_server(server, root, after_restart)
        print("PASS: process restart preserves the SQLite name and deleted objects stay missing")
    print("PASS: file mutation integration finished, 6 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)

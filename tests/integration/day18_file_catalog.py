import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

from day16_download_integration import exchange, require


def document(method, target, payload=b""):
    status, headers, body = exchange(method, target, payload)
    require(headers.get("content-type") == "application/json", "JSON response type")
    require("content-disposition" not in headers, "metadata became an attachment")
    return status, headers, json.loads(body)


def run_checks(root):
    require(document("GET", "/api/files")[::2] == (200, {"files": []}), "empty list")
    print("PASS: empty list is a JSON array")
    expected = []
    for payload in (b"", b"A\x00B\r\n"):
        status, headers, _ = exchange("POST", "/api/files?name=ignored", payload)
        require(status == 201, "upload compatibility")
        file_id = headers.get("x-file-id", "")
        require(re.fullmatch(r"upload-[A-Za-z0-9]{6}", file_id), "upload ID")
        item = {"id": file_id, "name": file_id + ".bin", "size": len(payload)}
        expected.append(item)
        status, _, actual = document("GET", f"/api/files/{file_id}")
        require(status == 200 and actual == item, "detail schema or values")
        require(type(actual["size"]) is int, "size is not a JSON integer")
        status, _, body = exchange("GET", f"/api/files/{file_id}/content")
        require(status == 200 and body == payload, "download route regressed")
    expected.sort(key=lambda item: item["id"])
    status, _, actual = document("GET", "/api/files")
    require(status == 200 and
            sorted(actual["files"], key=lambda item: item["id"]) == expected,
            "list schema or values")
    print("PASS: uploads, sorted list, details and downloads agree")

    objects = root / "data" / "objects"
    require(not ({item["id"] for item in expected} &
                 {"upload-Link01", "upload-Dir001", "upload-Fifo01", "upload-Miss01"}),
            "fixture ID collision")
    (root / "data" / "uploads" / "upload-Temp01").write_bytes(b"unfinished")
    (objects / "not-an-id").write_bytes(b"ignore")
    (objects / "upload-Dir001").mkdir()
    secret = root / "outside.txt"
    secret.write_bytes(b"secret")
    (objects / "upload-Link01").symlink_to(secret)
    os.mkfifo(objects / "upload-Fifo01")
    require(document("GET", "/api/files")[::2] == (200, {"files": expected}),
            "nonobjects appeared in list")
    for file_id in ("upload-Link01", "upload-Dir001", "upload-Fifo01", "upload-Miss01"):
        require(document("GET", f"/api/files/{file_id}")[0] == 404, "nonobject detail")
    print("PASS: temporary files, invalid names, links and nonregular objects stay hidden")

    for path in ("/api/files", f'/api/files/{expected[0]["id"]}'):
        status, get_headers, body = exchange("GET", path)
        head_status, head_headers, head_body = exchange("HEAD", path)
        require(status == head_status == 200 and get_headers == head_headers and not head_body,
                "metadata HEAD policy")
        require(int(head_headers["content-length"]) == len(body), "HEAD JSON length")
        require(document("GET", path, b"X")[0] == 400, "metadata request body")
    require(document("GET", "/api/files/..")[0] == 400, "invalid ID")
    status, headers, _ = document("POST", f'/api/files/{expected[0]["id"]}')
    require(status == 405 and headers.get("allow") == "GET, HEAD, PATCH, DELETE", "detail method")
    require(document("PUT", "/api/files")[0] == 501, "unsupported method")
    print("PASS: HEAD, invalid IDs, request bodies and method policy")

    moved = root / "objects-offline"
    objects.rename(moved)
    try:
        status, _, error = document("GET", "/api/files")
        require(status == 500 and error["error"]["code"] == "internal_error",
                "missing storage directory was reported as empty list")
        require(str(root) not in json.dumps(error), "internal path leaked")
        status, _, body = exchange("GET", "/health")
        require(status == 200 and body == b"OK\n", "service stopped after storage error")
    finally:
        moved.rename(objects)
    print("PASS: storage errors return 500 and service remains available")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: real NetDisk catalog integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    with tempfile.TemporaryDirectory(prefix="netdisk-day18-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
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
                run_checks(root)
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
    print("PASS: file catalog integration finished, 5 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)

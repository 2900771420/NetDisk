import argparse
import http.client
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time


ADDRESS = ("127.0.0.1", 8080)
TIMEOUT = 5


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def wait_for(predicate, message):
    deadline = time.monotonic() + TIMEOUT
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise RuntimeError(message)


def upload_header(length):
    return (
        "POST /api/files HTTP/1.1\r\n"
        "Host: localhost\r\n"
        f"Content-Length: {length}\r\n"
        "Connection: close\r\n\r\n"
    ).encode("ascii")


def receive(sock):
    with http.client.HTTPResponse(sock) as response:
        response.begin()
        body = response.read(4097)
        headers = {name.lower(): value for name, value in response.getheaders()}
        require(len(body) <= 4096, "response exceeds test limit")
        require(headers.get("content-length") == str(len(body)), "response length mismatch")
        require(headers.get("connection", "").lower() == "close", "missing Connection close")
        return response.status, headers, body


def exchange(request):
    with socket.create_connection(ADDRESS, timeout=TIMEOUT) as sock:
        sock.sendall(request)
        return receive(sock)


def run_checks(root):
    uploads = root / "data" / "uploads"
    objects = root / "data" / "objects"
    expected = set()

    def object_names():
        return {path.name for path in objects.iterdir()}

    def clean():
        return not list(uploads.iterdir()) and object_names() == expected

    def check_created(result, payload):
        status, headers, body = result
        require(status == 201, f"upload returned {status}")
        require(headers.get("content-type") == "application/json", "wrong response type")
        require(json.loads(body) == {"status": "created"}, "wrong creation result")
        file_id = headers.get("x-file-id", "")
        require(re.fullmatch(r"upload-[A-Za-z0-9]{6}", file_id), "unsafe or missing file ID")
        require(file_id not in expected, "file ID reused")
        require((objects / file_id).read_bytes() == payload, "published bytes differ")
        expected.add(file_id)
        wait_for(clean, "temporary file leaked or unexpected object appeared")

    for payload in (b"", "网盘测试\n".encode("utf-8"), bytes(range(256)) * 256):
        check_created(exchange(upload_header(len(payload)) + payload), payload)
    print("PASS: empty, UTF-8 text and 65536-byte binary uploads")

    def wait_partial(prefix):
        def written():
            require(object_names() == expected, "incomplete body was published")
            paths = list(uploads.iterdir())
            return len(paths) == 1 and paths[0].read_bytes() == prefix
        wait_for(written, "server did not write the partial body")

    payload = b"begin\x00body\r\nend"
    with socket.create_connection(ADDRESS, timeout=TIMEOUT) as sock:
        sock.sendall(upload_header(len(payload)) + payload[:3])
        wait_partial(payload[:3])
        sock.sendall(payload[3:])
        check_created(receive(sock), payload)
    print("PASS: partial body stayed temporary until completion")

    with socket.create_connection(ADDRESS, timeout=TIMEOUT) as sock:
        sock.sendall(upload_header(10) + b"AB")
        wait_partial(b"AB")
        sock.shutdown(socket.SHUT_WR)
        status, headers, body = receive(sock)
        require(status == 400 and "x-file-id" not in headers, "incomplete upload accepted")
        require(json.loads(body)["error"]["code"] == "bad_request", "wrong EOF error")
    wait_for(clean, "incomplete upload left temporary or published data")
    status, _, body = exchange(b"GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n")
    require(status == 200 and body == b"OK\n", "service failed after incomplete upload")
    require(clean(), "health check changed stored files")
    print("PASS: incomplete upload cleaned and later health succeeded")
    return len(expected)


def stop_server(process):
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
        print("SKIP: real NetDisk upload integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    with tempfile.TemporaryDirectory(prefix="netdisk-day14-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        log_path = root / "server.log"
        with log_path.open("wb") as log:
            process = subprocess.Popen(
                [str(server)], cwd=root, stdin=subprocess.DEVNULL,
                stdout=log, stderr=subprocess.STDOUT,
            )
            try:
                def ready():
                    require(process.poll() is None, "test server exited before listening")
                    return "listening on 127.0.0.1:8080" in log_path.read_text(
                        encoding="utf-8", errors="replace"
                    )
                wait_for(ready, "test server did not start listening")
                published = run_checks(root)
            except Exception:
                print("Server log:", file=sys.stderr)
                print(log_path.read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
                raise
            finally:
                stop_server(process)
    print(f"PASS: upload integration finished, 6 checks, {published} published files")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)

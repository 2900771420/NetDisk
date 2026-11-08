import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

from day16_download_integration import exchange, require


def run_checks():
    file_id = ""
    for payload in (b"", "响应头测试\n".encode("utf-8"), bytes(range(256)) * 256):
        status, headers, _ = exchange("POST", "/api/files", payload)
        require(status == 201, "upload failed")
        file_id = headers.get("x-file-id", "")
        require(re.fullmatch(r"upload-[A-Za-z0-9]{6}", file_id), "invalid uploaded ID")
        target = f"/api/files/{file_id}/content"
        status, get_headers, body = exchange("GET", target)
        require(status == 200 and body == payload, "GET changed file content")
        require(get_headers.get("content-type") == "application/octet-stream", "binary type")
        require(get_headers.get("content-length") == str(len(payload)), "GET length")
        require(get_headers.get("content-disposition") ==
                f'attachment; filename="{file_id}.bin"', "download filename")
        status, head_headers, body = exchange("HEAD", target)
        require(status == 200 and not body and head_headers == get_headers,
                "HEAD headers differ from GET or contain a body")
    print("PASS: empty, UTF-8 and binary GET HEAD headers match")

    target = f"/api/files/{file_id}/content"
    for method in ("GET", "HEAD"):
        status, headers, _ = exchange(method, target + "?name=..%2Fbad%0D%0AX-Test%3Ayes")
        require(status == 200 and headers.get("content-disposition") ==
                f'attachment; filename="{file_id}.bin"' and "x-test" not in headers,
                "query affected response headers")
    print("PASS: query cannot change filename or inject headers")

    for invalid, expected in (("upload-Miss01", 404), ("..", 400)):
        path = f"/api/files/{invalid}/content"
        status, get_headers, get_body = exchange("GET", path)
        require(status == expected and get_headers.get("content-type") == "application/json",
                "GET error policy")
        status, head_headers, body = exchange("HEAD", path)
        require(status == expected and not body and head_headers == get_headers,
                "HEAD error differs from GET")
        require(head_headers.get("content-length") == str(len(get_body)) and
                "content-disposition" not in head_headers, "error download headers")
    print("PASS: HEAD 400 and 404 retain JSON length without a body")

    status, headers, _ = exchange("POST", target)
    require(status == 405 and headers.get("allow") == "GET, HEAD" and
            "content-disposition" not in headers, "method error headers")
    print("PASS: 405 has Allow and no attachment filename")

    for method in ("GET", "HEAD"):
        status, headers, _ = exchange(method, target, b"X")
        require(status == 400 and "content-disposition" not in headers,
                "nonempty request body accepted")
    print("PASS: GET and HEAD reject request bodies")

    status, _, body = exchange("GET", "/health")
    require(status == 200 and body == b"OK\n", "health after requests")
    print("PASS: service remains available")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: real NetDisk download header integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    with tempfile.TemporaryDirectory(prefix="netdisk-day17-") as directory:
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
                run_checks()
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
    print("PASS: download header integration finished, 6 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)

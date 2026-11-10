import argparse
from pathlib import Path
import os
import re
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def exchange(method, target, payload=b""):
    header = (
        f"{method} {target} HTTP/1.1\r\nHost: localhost\r\n"
        f"Content-Length: {len(payload)}\r\nConnection: close\r\n\r\n"
    ).encode("ascii")
    with socket.create_connection(("127.0.0.1", 8080), timeout=5) as sock:
        sock.sendall(header + payload)
        wire = bytearray()
        while True:
            block = sock.recv(16384)
            if not block:
                break
            wire.extend(block)
            require(len(wire) <= 1024 * 1024, "response exceeds test limit")
    head, separator, body = bytes(wire).partition(b"\r\n\r\n")
    require(separator, "missing response header terminator")
    lines = head.decode("ascii").split("\r\n")
    status = int(lines[0].split(" ")[1])
    headers = {}
    for line in lines[1:]:
        name, value = line.split(":", 1)
        require(name.lower() not in headers, "duplicate response header")
        headers[name.lower()] = value.strip()
    length = int(headers["content-length"])
    require(length >= 0, "negative response length")
    require(headers.get("connection", "").lower() == "close", "connection policy")
    require(not body if method == "HEAD" else len(body) == length,
            "body length mismatch or unwanted HEAD body")
    return status, headers, body


# ===== DAY 25 ADDED BEGIN 为磁盘夹具补充ready元数据 =====
def add_ready_metadata(root, file_id, size):
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        database.execute(
            "INSERT INTO files(id, name, size, state, created_at) "
            "VALUES(?, ?, ?, 'ready', ?)",
            (file_id, f"{file_id}.bin", size, max(0, int(time.time()))),
        )
# ===== DAY 25 ADDED END =====


def run_checks(root):
    objects = root / "data" / "objects"
    ids = []
    for payload in (b"", "下载测试\n".encode("utf-8"), bytes(range(256)) * 256):
        status, headers, _ = exchange("POST", "/api/files", payload)
        require(status == 201, "upload failed")
        file_id = headers.get("x-file-id", "")
        require(re.fullmatch(r"upload-[A-Za-z0-9]{6}", file_id), "invalid upload ID")
        require(file_id not in ids, "reused file ID")
        ids.append(file_id)
        status, headers, body = exchange("GET", f"/api/files/{file_id}/content")
        require(status == 200 and body == payload, "upload/download bytes differ")
        require(headers.get("content-type") == "application/octet-stream", "download type")
    print("PASS: empty, UTF-8 and 65536-byte binary round trips")

    large = bytes(range(256)) * 1024 + b"tail\x00"
    large_id = "upload-Large1"
    require(large_id not in ids, "fixture ID collision")
    (objects / large_id).write_bytes(large)
    # ===== DAY 25 ADDED BEGIN 未登记对象不再允许直接下载 =====
    add_ready_metadata(root, large_id, len(large))
    # ===== DAY 25 ADDED END =====
    status, _, body = exchange("GET", f"/api/files/{large_id}/content")
    require(status == 200 and body == large, "multi-buffer download differs")
    print("PASS: 262149-byte disk fixture download across buffer boundaries")

    status, headers, body = exchange("HEAD", f"/api/files/{large_id}/content")
    require(status == 200 and int(headers["content-length"]) == len(large) and not body,
            "HEAD size or body")
    missing = "upload-Miss01"
    require(not (objects / missing).exists(), "missing fixture collision")
    status, headers, _ = exchange("HEAD", f"/api/files/{missing}/content")
    require(status == 404 and int(headers["content-length"]) > 0, "HEAD error")
    print("PASS: HEAD success and error contain no body")

    for file_id in ("", "..", "../upload-Abc123", "upload-abc/de",
                    "upload-%2e%2e", "upload-Abc1234", "upload-Abc12"):
        status, _, _ = exchange("GET", f"/api/files/{file_id}/content")
        require(status == 400, f"unsafe ID returned {status}: {file_id}")
    print("PASS: seven invalid IDs rejected before opening files")

    outside = root / "outside.txt"
    outside.write_bytes(b"outside secret")
    require(not set(ids) & {"upload-Link01", "upload-Dir001", "upload-Fifo01"},
            "non-regular fixture ID collision")
    (objects / "upload-Link01").symlink_to(outside)
    (objects / "upload-Dir001").mkdir()
    os.mkfifo(objects / "upload-Fifo01")
    # ===== DAY 25 ADDED BEGIN 非普通文件检查需要先通过ready可见性门槛 =====
    add_ready_metadata(root, "upload-Link01", len(b"outside secret"))
    add_ready_metadata(root, "upload-Dir001", 0)
    add_ready_metadata(root, "upload-Fifo01", 0)
    # ===== DAY 25 ADDED END =====
    for file_id in (missing, "upload-Link01", "upload-Dir001", "upload-Fifo01"):
        status, _, body = exchange("GET", f"/api/files/{file_id}/content")
        require(status == 404 and b"outside secret" not in body,
                "missing or non-regular object accepted")
    print("PASS: missing object, symlink, directory and FIFO rejected")

    target = f"/api/files/{large_id}/content"
    status, headers, _ = exchange("POST", target)
    require(status == 405 and headers.get("allow") == "GET, HEAD", "method policy")
    require(exchange("PUT", target)[0] == 501, "unsupported method policy")
    require(exchange("GET", target, b"X")[0] == 400, "GET body accepted")
    print("PASS: method and request body policy")

    require(exchange("GET", target + "/extra")[0] == 404, "unknown route")
    status, _, body = exchange("GET", target + "?name=../../outside.txt")
    require(status == 200 and body == large, "query altered object selection")
    print("PASS: exact route matching and ignored query")

    status, _, body = exchange("GET", "/health")
    require(status == 200 and body == b"OK\n", "health after errors")
    require(not list((root / "data" / "uploads").iterdir()), "temporary upload leaked")
    expected = set(ids) | {large_id, "upload-Link01", "upload-Dir001", "upload-Fifo01"}
    require({p.name for p in objects.iterdir()} == expected, "unexpected storage changes")
    print("PASS: service remains usable and no unexpected objects appear")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: real NetDisk download integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    with tempfile.TemporaryDirectory(prefix="netdisk-day16-") as directory:
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
    print("PASS: download integration finished, 8 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)

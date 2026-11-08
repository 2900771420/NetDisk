import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time

from day16_download_integration import exchange, require


ADDRESS = ("127.0.0.1", 8080)
TIMEOUT = 5


def wait_for(process, predicate, message):
    deadline = time.monotonic() + TIMEOUT
    while time.monotonic() < deadline:
        require(process.poll() is None, "test server exited unexpectedly")
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


def reset_connection(sock):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    sock.close()


def wait_for_health(process):
    def healthy():
        try:
            status, _, body = exchange("GET", "/health")
            return status == 200 and body == b"OK\n"
        except (OSError, RuntimeError):
            return False

    wait_for(process, healthy, "service did not recover for a later health request")


def run_checks(root, process):
    uploads = root / "data" / "uploads"
    objects = root / "data" / "objects"

    missing = "upload-Miss20"
    require(not (objects / missing).exists(), "missing fixture collision")
    require(exchange("GET", f"/api/files/{missing}/content")[0] == 404,
            "missing download did not return 404")
    wait_for_health(process)
    print("PASS: a missing object returns 404 and the service accepts the next request")

    denied = objects / "upload-Deny20"
    denied.write_bytes(b"permission fixture")
    denied.chmod(0)
    try:
        if os.geteuid() == 0:
            print("SKIP DETAIL: root bypasses the unreadable-file permission check")
        else:
            require(exchange("GET", "/api/files/upload-Deny20/content")[0] == 500,
                    "unreadable object did not return 500")
            wait_for_health(process)
    finally:
        denied.chmod(0o600)
    require(exchange("GET", "/api/files/upload-Deny20/content")[2] ==
            b"permission fixture", "restored object is not downloadable")
    if os.geteuid() == 0:
        print("PASS: the permission fixture is downloadable after restoration")
    else:
        print("PASS: an unreadable object is isolated to its request and recovery is observable")

    uploads.rmdir()
    try:
        status, _, body = exchange("POST", "/api/files", b"cannot create temporary file")
        require(status == 500 and json.loads(body)["error"]["code"] == "internal_error",
                "missing upload directory did not return 500")
        require({path.name for path in objects.iterdir()} == {denied.name},
                "failed temporary creation changed published objects")
    finally:
        uploads.mkdir(exist_ok=True)
    wait_for_health(process)
    print("PASS: temporary-file creation failure returns 500 without publishing an object")

    saved_objects = root / "data" / "objects.saved"
    objects.rename(saved_objects)
    try:
        status, _, _ = exchange("POST", "/api/files", b"cannot publish")
        require(status == 500, "missing object directory did not fail publication")
        wait_for(process, lambda: not list(uploads.iterdir()),
                 "failed publication leaked a temporary file")
    finally:
        saved_objects.rename(objects)
    wait_for_health(process)
    require({path.name for path in objects.iterdir()} == {denied.name},
            "failed publication changed the object set")
    print("PASS: publication failure cleans its temporary file and later requests succeed")

    before = {path.name for path in objects.iterdir()}
    partial = b"partial-body"
    sock = socket.create_connection(ADDRESS, timeout=TIMEOUT)
    try:
        sock.sendall(upload_header(4096) + partial)

        def partial_written():
            paths = list(uploads.iterdir())
            return len(paths) == 1 and paths[0].read_bytes() == partial

        wait_for(process, partial_written, "server did not write the partial request body")
        reset_connection(sock)
        sock = None
    finally:
        if sock is not None:
            sock.close()
    wait_for(process, lambda: not list(uploads.iterdir()),
             "reset upload leaked its temporary file")
    require({path.name for path in objects.iterdir()} == before,
            "reset upload published an incomplete object")
    wait_for_health(process)
    print("PASS: TCP reset before commit removes the temporary file and publishes nothing")

    committed_payload = b"commit-before-response\x00"
    sock = socket.create_connection(ADDRESS, timeout=TIMEOUT)
    try:
        sock.sendall(upload_header(len(committed_payload)) + committed_payload)

        def committed_object():
            created = [path for path in objects.iterdir() if path.name not in before]
            return (len(created) == 1 and created[0].is_file() and
                    created[0].read_bytes() == committed_payload)

        wait_for(process, committed_object, "complete upload was not published")
        reset_connection(sock)
        sock = None
    finally:
        if sock is not None:
            sock.close()
    wait_for_health(process)
    created = [path for path in objects.iterdir() if path.name not in before]
    require(len(created) == 1 and created[0].read_bytes() == committed_payload,
            "client reset rolled back a committed object")
    print("PASS: client loss after commit does not roll back the published object")

    large_id = "upload-Large2"
    large_path = objects / large_id
    large_path.write_bytes(bytes(range(256)) * (64 * 1024))
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.settimeout(TIMEOUT)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        sock.connect(ADDRESS)
        sock.sendall((
            f"GET /api/files/{large_id}/content HTTP/1.1\r\n"
            "Host: localhost\r\nConnection: close\r\n\r\n"
        ).encode("ascii"))
        wire = bytearray()
        while b"\r\n\r\n" not in wire:
            block = sock.recv(4096)
            require(block, "download connection closed before the response header")
            wire.extend(block)
            require(len(wire) <= 65536, "download response header is too large")
        require(wire.startswith(b"HTTP/1.1 200 OK\r\n"), "large download did not start")
        reset_connection(sock)
        sock = None
    finally:
        if sock is not None:
            sock.close()
    wait_for_health(process)
    require(large_path.stat().st_size == 16 * 1024 * 1024,
            "interrupted download changed the stored object")
    print("PASS: reset during a large download closes that connection and the service continues")


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
        print("SKIP: real NetDisk failure recovery integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    with tempfile.TemporaryDirectory(prefix="netdisk-day20-") as directory:
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
                        encoding="utf-8", errors="replace")

                wait_for(process, ready, "test server did not start listening")
                run_checks(root, process)
            except Exception:
                print(log_path.read_text(encoding="utf-8", errors="replace"),
                      file=sys.stderr)
                raise
            finally:
                stop_server(process)
    print("PASS: failure recovery integration finished, 7 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)

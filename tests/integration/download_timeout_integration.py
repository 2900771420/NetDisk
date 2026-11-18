import argparse
import hashlib
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time


FILE_ID = "upload-Abc123"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def receive_request(connection):
    connection.settimeout(2)
    request = bytearray()
    while b"\r\n\r\n" not in request:
        block = connection.recv(4096)
        require(block, "client closed before request head completed")
        request.extend(block)
        require(len(request) <= 8192, "client request head exceeded limit")
    return bytes(request)


class OneShotServer:
    def __init__(self, handler):
        self.handler = handler
        self.error = None
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(3)
        self.port = self.listener.getsockname()[1]
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self):
        try:
            connection, _ = self.listener.accept()
            with connection:
                self.handler(connection, self.port)
        except Exception as error:
            self.error = error
        finally:
            self.listener.close()

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, exception_type, exception, traceback):
        self.thread.join(timeout=4)
        require(not self.thread.is_alive(), "fake server did not stop")
        if self.error is not None:
            raise self.error


def run_client(client, port, timeout_ms, output):
    started = time.monotonic()
    result = subprocess.run(
        [
            str(client),
            "--host", "127.0.0.1",
            "--port", str(port),
            "--timeout-ms", str(timeout_ms),
            FILE_ID,
            str(output),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=5,
        check=False,
    )
    return result, time.monotonic() - started


def make_head(status, reason, length, etag, content_range=None):
    fields = [
        f"HTTP/1.1 {status} {reason}",
        f"Content-Length: {length}",
        f"ETag: {etag}",
        "Connection: close",
    ]
    if content_range is not None:
        fields.insert(3, f"Content-Range: {content_range}")
    return ("\r\n".join(fields) + "\r\n\r\n").encode("ascii")


def run_checks(client, root):
    payload = bytes(range(256)) * 128
    prefix_size = 7000
    digest = hashlib.sha256(payload).hexdigest()
    etag = f'"{digest}"'
    output = root / "timed-download.bin"
    # ===== DAY 35 ADDED BEGIN 最终路径保留旧版本 临时路径接收新版本 =====
    published_before = b"previous-published-version"
    output.write_bytes(published_before)
    partial = Path(str(output) + ".netdisk-part")
    # ===== DAY 35 ADDED END =====

    def stalled_response(connection, port):
        request = receive_request(connection)
        require(
            f"Host: 127.0.0.1:{port}\r\n".encode("ascii") in request,
            "configured Host header was not sent",
        )
        connection.sendall(
            make_head(200, "OK", len(payload), etag) + payload[:prefix_size]
        )
        time.sleep(0.8)

    with OneShotServer(stalled_response) as server:
        first, elapsed = run_client(client, server.port, 200, output)

    require(first.returncode == 1, "stalled transfer did not fail")
    require("timed out" in first.stderr, "timeout diagnostic was not reported")
    require(elapsed < 2, "configured timeout did not bound the stalled transfer")
    # ===== DAY 35 MODIFIED BEGIN 超时只改变临时产物不暴露未完成正文 =====
    require(output.read_bytes() == published_before,
            "timed out transfer changed the published output")
    require(partial.read_bytes() == payload[:prefix_size],
            "timed out transfer did not preserve the partial prefix")
    # ===== DAY 35 MODIFIED END =====
    state = Path(str(output) + ".netdisk-etag")
    require(state.read_text(encoding="ascii").strip() == etag,
            "timed out transfer did not preserve its ETag state")

    def resumed_response(connection, port):
        request = receive_request(connection)
        require(
            f"Host: 127.0.0.1:{port}\r\n".encode("ascii") in request,
            "second configured Host header was not sent",
        )
        require(
            f"Range: bytes={prefix_size}-\r\n".encode("ascii") in request,
            "retry did not request the missing suffix",
        )
        require(
            f"If-Range: {etag}\r\n".encode("ascii") in request,
            "retry did not send the preserved ETag",
        )
        remaining = payload[prefix_size:]
        connection.sendall(
            make_head(
                206,
                "Partial Content",
                len(remaining),
                etag,
                f"bytes {prefix_size}-{len(payload) - 1}/{len(payload)}",
            ) + remaining
        )

    with OneShotServer(resumed_response) as server:
        second, _ = run_client(client, server.port, 1000, output)

    require(
        second.returncode == 0,
        f"retry failed code={second.returncode} "
        f"stdout={second.stdout!r} stderr={second.stderr!r}",
    )
    require("resumed with 206" in second.stdout,
            "retry did not report the 206 resume path")
    require(output.read_bytes() == payload,
            "retry did not reconstruct the complete payload")
    # ===== DAY 35 ADDED BEGIN 完成后临时名称必须已经原子发布 =====
    require(not partial.exists(),
            "verified partial file still exists after publication")
    # ===== DAY 35 ADDED END =====
    require(hashlib.sha256(output.read_bytes()).hexdigest() == digest,
            "completed retry digest differs")
    print("PASS: timeout keeps published output and retry atomically replaces it")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("client", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: download timeout integration requires Linux")
        return 77
    client = args.client.resolve(strict=True)
    require(client.is_file(), "client executable is not a file")

    with tempfile.TemporaryDirectory(prefix="netdisk-download-timeout-") as directory:
        run_checks(client, Path(directory))

    print("PASS: download publication integration finished 1 workflow")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)

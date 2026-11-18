# ===== DAY 31 ADDED BEGIN 验证真实服务单范围下载和安全回退 =====
import argparse
from pathlib import Path
import socket
import sys
import tempfile

from day16_download_integration import exchange, require
from day19_file_mutation import run_server


def exchange_with_headers(method, target, fields):
    lines = [f"{method} {target} HTTP/1.1", "Host: localhost"]
    lines.extend(fields)
    lines.extend(["Content-Length: 0", "Connection: close", "", ""])
    request = "\r\n".join(lines).encode("ascii")
    with socket.create_connection(("127.0.0.1", 8080), timeout=5) as sock:
        sock.sendall(request)
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
    require(not body if method == "HEAD" else len(body) == length,
            "body length differs or HEAD returned a body")
    return status, headers, body


def verify_ranges(payload, target, etag):
    cases = [
        ("bytes=2-7", 2, 7),
        ("bytes=250-", 250, 255),
        ("bytes=-5", 251, 255),
        ("bytes=253-999", 253, 255),
        ("bytes=-999", 0, 255),
    ]
    for value, start, end in cases:
        status, headers, body = exchange_with_headers(
            "GET", target, [f"Range: {value}"])
        require(status == 206 and body == payload[start:end + 1],
                f"range bytes differ for {value}")
        require(headers.get("content-range") ==
                    f"bytes {start}-{end}/{len(payload)}",
                f"Content-Range differs for {value}")
        require(int(headers["content-length"]) == end - start + 1 and
                headers.get("accept-ranges") == "bytes" and
                headers.get("etag") == etag,
                f"range response metadata differs for {value}")
    print("PASS: exact open and suffix ranges return the selected bytes")


def verify_head_and_fallback(payload, target, etag):
    status, headers, body = exchange_with_headers(
        "HEAD", target, ["Range: bytes=10-19"])
    require(status == 206 and not body and headers.get("content-range") ==
                f"bytes 10-19/{len(payload)}" and
            int(headers["content-length"]) == 10,
            "HEAD range metadata differs")

    for fields in (["Range: bytes=0-1, 4-5"],
                   ["Range: items=0-1"],
                   ["Range: bytes=0-1", "If-Range: \"different\""]):
        status, headers, body = exchange_with_headers("GET", target, fields)
        require(status == 200 and body == payload and
                "content-range" not in headers and headers.get("etag") == etag,
                "unsupported or conditional range did not safely return full content")
    print("PASS: HEAD range is bodyless and unsupported range forms fall back to 200")


def verify_errors(payload, target, etag, empty_target):
    for value in (f"bytes={len(payload)}-", "bytes=-0"):
        status, headers, body = exchange_with_headers(
            "GET", target, [f"Range: {value}"])
        require(status == 416 and headers.get("content-range") ==
                    f"bytes */{len(payload)}" and headers.get("etag") == etag and
                b"range_not_satisfiable" in body,
                f"unsatisfiable range did not return 416 for {value}")
    status, headers, body = exchange_with_headers(
        "HEAD", empty_target, ["Range: bytes=0-"])
    require(status == 416 and not body and
            headers.get("content-range") == "bytes */0",
            "empty file HEAD range did not return bodyless 416")

    for fields in (["Range: bytes=8-2"],
                   ["Range: bytes=abc"],
                   ["Range: bytes=0-1", "Range: bytes=2-3"]):
        require(exchange_with_headers("GET", target, fields)[0] == 400,
                "malformed or duplicate Range was accepted")
    require(exchange("GET", "/health")[0] == 200,
            "range errors stopped the service")
    print("PASS: invalid ranges return 400 and unsatisfiable ranges return 416")


def run_checks():
    payload = bytes(range(256))
    status, headers, _ = exchange("POST", "/api/files", payload)
    require(status == 201, "fixture upload failed")
    file_id = headers.get("x-file-id", "")
    etag = f'"{headers.get("x-file-sha256", "")}"'
    target = f"/api/files/{file_id}/content"

    status, full_headers, body = exchange("GET", target)
    require(status == 200 and body == payload and
            full_headers.get("accept-ranges") == "bytes" and
            full_headers.get("etag") == etag,
            "full response did not advertise byte ranges")
    empty_status, empty_headers, _ = exchange("POST", "/api/files", b"")
    require(empty_status == 201, "empty fixture upload failed")
    empty_target = f"/api/files/{empty_headers['x-file-id']}/content"

    verify_ranges(payload, target, etag)
    verify_head_and_fallback(payload, target, etag)
    verify_errors(payload, target, etag, empty_target)
    print("PASS: full downloads advertise byte range support")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: range integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")

    with tempfile.TemporaryDirectory(prefix="netdisk-day31-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        run_server(server, root, run_checks)

    print("PASS: range integration finished 4 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
# ===== DAY 31 ADDED END =====

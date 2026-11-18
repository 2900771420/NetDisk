# ===== DAY 32 ADDED BEGIN 验证真实服务条件范围下载 =====
import argparse
from pathlib import Path
import sys
import tempfile

from day16_download_integration import exchange, require
from day19_file_mutation import run_server
from day31_range import exchange_with_headers


def run_checks():
    payload = bytes(range(64))
    status, headers, _ = exchange("POST", "/api/files", payload)
    require(status == 201, "fixture upload failed")
    target = f"/api/files/{headers['x-file-id']}/content"
    etag = f'"{headers["x-file-sha256"]}"'

    status, response_headers, body = exchange_with_headers(
        "GET", target, ["Range: bytes=10-19", f"If-Range: {etag}"])
    require(status == 206 and body == payload[10:20],
            "matching If-Range did not return selected bytes")
    require(response_headers.get("content-range") ==
                f"bytes 10-19/{len(payload)}" and
            response_headers.get("etag") == etag,
            "matching If-Range response metadata differs")

    status, response_headers, body = exchange_with_headers(
        "HEAD", target, ["Range: bytes=5-8", f"If-Range: {etag}"])
    require(status == 206 and not body and
            response_headers.get("content-range") ==
                f"bytes 5-8/{len(payload)}" and
            int(response_headers["content-length"]) == 4,
            "matching HEAD If-Range metadata differs")
    print("PASS: matching strong If-Range returns 206")

    fallbacks = [
        '"different"',
        f"W/{etag}",
        "Sun, 06 Nov 1994 08:49:37 GMT",
        headers["x-file-sha256"],
    ]
    for validator in fallbacks:
        status, response_headers, body = exchange_with_headers(
            "GET", target,
            ["Range: bytes=10-19", f"If-Range: {validator}"])
        require(status == 200 and body == payload and
                "content-range" not in response_headers and
                response_headers.get("etag") == etag,
                f"nonmatching If-Range did not return full content: {validator}")
    status, response_headers, body = exchange_with_headers(
        "GET", target,
        ["Range: bytes=9-2", 'If-Range: "different"'])
    require(status == 200 and body == payload and
            "content-range" not in response_headers,
            "nonmatching If-Range did not suppress invalid Range")
    print("PASS: nonmatching weak date and malformed If-Range return 200")

    require(exchange_with_headers(
        "GET", target,
        [f"Range: bytes={len(payload)}-", f"If-Range: {etag}"])[0] == 416,
        "matching If-Range did not preserve unsatisfiable Range semantics")
    require(exchange_with_headers(
        "GET", target,
        ["Range: bytes=9-2", f"If-Range: {etag}"])[0] == 400,
        "matching If-Range did not preserve invalid Range semantics")
    require(exchange_with_headers(
        "GET", target,
        ["Range: bytes=0-1", f"If-Range: {etag}",
         f"If-Range: {etag}"])[0] == 400,
        "duplicate If-Range was accepted")
    require(exchange("GET", "/health")[0] == 200,
            "If-Range errors stopped the service")
    print("PASS: matched Range errors remain 400 or 416")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: If-Range integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")

    with tempfile.TemporaryDirectory(prefix="netdisk-day32-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        run_server(server, root, run_checks)

    print("PASS: If-Range integration finished 3 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
# ===== DAY 32 ADDED END =====

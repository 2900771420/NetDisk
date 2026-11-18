# ===== DAY 33 ADDED BEGIN 验证真实客户端的206追加200覆盖与416完成 =====
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

from day16_download_integration import exchange, require
from day19_file_mutation import run_server


def upload(payload):
    status, headers, _ = exchange("POST", "/api/files", payload)
    require(status == 201, "fixture upload failed")
    return headers["x-file-id"], f'"{headers["x-file-sha256"]}"'


def run_client(client, file_id, output):
    result = subprocess.run(
        [str(client), file_id, str(output)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=10,
        check=False,
    )
    require(
        result.returncode == 0,
        f"client failed code={result.returncode} "
        f"stdout={result.stdout!r} stderr={result.stderr!r}",
    )
    return result.stdout


def write_state(output, etag):
    Path(str(output) + ".netdisk-etag").write_text(etag + "\n", encoding="ascii")


def read_state(output):
    return Path(str(output) + ".netdisk-etag").read_text(
        encoding="ascii").strip()


def run_checks(client, root):
    first_payload = bytes(range(256)) * 16
    first_id, first_etag = upload(first_payload)
    resumed = root / "resumed.bin"
    resumed.write_bytes(first_payload[:1300])
    write_state(resumed, first_etag)

    output = run_client(client, first_id, resumed)
    require("resumed with 206" in output,
            "client did not report the 206 append path")
    require(resumed.read_bytes() == first_payload,
            "206 append did not reconstruct the original file")
    require(read_state(resumed) == first_etag,
            "206 append changed the current ETag state")
    require(hashlib.sha256(resumed.read_bytes()).hexdigest() ==
            first_etag[1:-1], "206 result digest differs")
    print("PASS: client appends only a validated 206 response")

    second_payload = b"new-version-" + bytes(reversed(range(256))) * 12
    second_id, second_etag = upload(second_payload)
    replaced = root / "replaced.bin"
    replaced.write_bytes(first_payload[:900])
    write_state(replaced, first_etag)

    output = run_client(client, second_id, replaced)
    require("replaced with 200" in output,
            "client did not report the 200 replacement path")
    require(replaced.read_bytes() == second_payload,
            "200 response was appended instead of replacing the old prefix")
    require(read_state(replaced) == second_etag,
            "200 replacement did not store the new ETag")
    print("PASS: version mismatch makes the client replace on 200")

    before = replaced.read_bytes()
    output = run_client(client, second_id, replaced)
    require("already complete after 416" in output,
            "client did not recognize the complete local file")
    require(replaced.read_bytes() == before and read_state(replaced) == second_etag,
            "416 completion path changed the verified local file")
    print("PASS: matching 416 verifies an already complete file")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    parser.add_argument("client", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: resume download integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    client = args.client.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    require(client.is_file(), "client executable is not a file")

    with tempfile.TemporaryDirectory(prefix="netdisk-day33-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        run_server(server, root, lambda: run_checks(client, root))

    print("PASS: resume download integration finished 3 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
# ===== DAY 33 ADDED END =====

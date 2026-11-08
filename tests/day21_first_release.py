import argparse
import json
from pathlib import Path
import re
import sys
import tempfile

from day16_download_integration import exchange, require
from day19_file_mutation import delete, patch, run_server


def run_checks(root):
    uploads = root / "data" / "uploads"
    objects = root / "data" / "objects"

    status, _, body = exchange("GET", "/health")
    require(status == 200 and body == b"OK\n", "initial health failed")
    status, _, body = exchange("GET", "/api/files")
    require(status == 200 and json.loads(body) == {"files": []}, "initial list not empty")
    print("PASS: service starts healthy with an empty public catalog")

    payload = bytes(range(256)) * 256
    status, headers, body = exchange("POST", "/api/files", payload)
    file_id = headers.get("x-file-id", "")
    require(status == 201 and json.loads(body) == {"status": "created"},
            "upload response")
    require(re.fullmatch(r"upload-[A-Za-z0-9]{6}", file_id), "unsafe upload ID")
    object_path = objects / file_id
    require(object_path.read_bytes() == payload, "published bytes differ")
    require(not list(uploads.iterdir()), "successful upload left a temporary name")
    print("PASS: 65536-byte binary upload publishes one safe immutable object")

    expected_default = {"id": file_id, "name": file_id + ".bin", "size": len(payload)}
    status, _, body = exchange("GET", f"/api/files/{file_id}")
    require(status == 200 and json.loads(body) == expected_default, "default detail")
    status, _, body = exchange("GET", "/api/files")
    require(status == 200 and json.loads(body) == {"files": [expected_default]},
            "catalog after upload")
    print("PASS: list and detail expose the same ID, default name and exact size")

    display_name = '第二十一天-闭环"演示.bin'
    status, _, body = patch(f"/api/files/{file_id}", {"name": display_name})
    expected_renamed = {"id": file_id, "name": display_name, "size": len(payload)}
    require(status == 200 and json.loads(body) == expected_renamed, "rename response")
    require(json.loads(exchange("GET", f"/api/files/{file_id}")[2]) == expected_renamed,
            "renamed detail")
    require(json.loads(exchange("GET", "/api/files")[2]) == {"files": [expected_renamed]},
            "renamed catalog")
    require(object_path.read_bytes() == payload, "rename changed object bytes")
    print("PASS: UTF-8 display rename changes persistent metadata only")

    target = f"/api/files/{file_id}/content"
    status, head_headers, head_body = exchange("HEAD", target)
    require(status == 200 and not head_body, "download HEAD")
    require(int(head_headers["content-length"]) == len(payload), "download HEAD size")
    require(head_headers.get("content-type") == "application/octet-stream",
            "download content type")
    require(head_headers.get("content-disposition") ==
            f'attachment; filename="{file_id}.bin"', "download safe filename")
    status, get_headers, body = exchange("GET", target)
    require(status == 200 and body == payload, "download bytes differ")
    require(get_headers == head_headers, "GET and HEAD headers differ")
    print("PASS: GET and HEAD agree and download reproduces every binary byte")

    status, headers, body = delete(f"/api/files/{file_id}")
    require(status == 204 and not headers.get("content-length") and not body,
            "delete response")
    require(not object_path.exists(), "deleted object remains on disk")
    require(json.loads(exchange("GET", "/api/files")[2]) == {"files": []},
            "deleted object remains listed")
    print("PASS: DELETE removes the object and returns an empty 204 response")

    for method in ("GET", "HEAD"):
        require(exchange(method, f"/api/files/{file_id}")[0] == 404,
                "deleted detail remained accessible")
        require(exchange(method, target)[0] == 404,
                "deleted content remained accessible")
    require(delete(f"/api/files/{file_id}")[0] == 404, "repeat delete policy")
    print("PASS: deleted detail and content remain 404 for later requests")

    status, _, body = exchange("GET", "/health")
    require(status == 200 and body == b"OK\n", "final health failed")
    require(not list(uploads.iterdir()) and not list(objects.iterdir()),
            "workflow left storage artifacts")
    print("PASS: service remains healthy and the isolated storage returns to empty")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: real NetDisk first release integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    with tempfile.TemporaryDirectory(prefix="netdisk-day21-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        run_server(server, root, lambda: run_checks(root))
    print("PASS: first release workflow finished, 7 steps")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)

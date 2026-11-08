import argparse
import json
from pathlib import Path
import re
import sqlite3
import sys
import tempfile

from day16_download_integration import exchange, require
from day19_file_mutation import delete, patch, run_server


def create_and_rename(root, payload, name):
    status, headers, _ = exchange("POST", "/api/files", payload)
    file_id = headers.get("x-file-id", "")
    require(status == 201 and re.fullmatch(r"upload-[A-Za-z0-9]{6}", file_id),
            "upload did not create a safe ID")
    status, _, body = patch(f"/api/files/{file_id}", {"name": name})
    expected = {"id": file_id, "name": name, "size": len(payload)}
    require(status == 200 and json.loads(body) == expected, "rename response")
    require((root / "data" / "objects" / file_id).read_bytes() == payload,
            "metadata write changed object bytes")
    print("PASS: upload creates ready metadata and bound rename preserves exact SQL-like text")
    return file_id


def inspect_database(root, file_id, payload, name):
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        rows = database.execute(
            "SELECT id, name, size, state, created_at FROM files").fetchall()
        require(len(rows) == 1, "unexpected metadata row count")
        row = rows[0]
        require(row[0] == file_id and row[1] == name and row[2] == len(payload) and
                row[3] == "ready" and isinstance(row[4], int) and row[4] > 0,
                "persisted metadata values differ")
        require(database.execute("PRAGMA user_version").fetchone()[0] == 1,
                "schema version changed")
    print("PASS: database stores one exact ready row with size and creation time")


def read_after_restart(file_id, payload, name):
    expected = {"id": file_id, "name": name, "size": len(payload)}
    status, _, body = exchange("GET", f"/api/files/{file_id}")
    require(status == 200 and json.loads(body) == expected,
            "detail lost persisted metadata")
    status, _, body = exchange("GET", "/api/files")
    require(status == 200 and json.loads(body) == {"files": [expected]},
            "list lost persisted metadata")
    status, _, body = exchange("GET", f"/api/files/{file_id}/content")
    require(status == 200 and body == payload, "download changed after restart")
    require(delete(f"/api/files/{file_id}")[0] == 204, "delete after restart failed")
    print("PASS: restart restores list and detail before delete removes object and row")


def verify_deleted(file_id):
    require(json.loads(exchange("GET", "/api/files")[2]) == {"files": []},
            "deleted metadata returned after restart")
    require(exchange("GET", f"/api/files/{file_id}")[0] == 404,
            "deleted detail returned after restart")
    require(exchange("GET", f"/api/files/{file_id}/content")[0] == 404,
            "deleted content returned after restart")
    require(exchange("GET", "/health")[0] == 200, "service unhealthy after restart")
    print("PASS: a second restart keeps deleted metadata and content absent")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: metadata persistence integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    payload = b"day23\x00persistent\r\n"
    name = "report' SET state='deleting' -- 第二十三天.txt"
    with tempfile.TemporaryDirectory(prefix="netdisk-day23-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        file_id = run_server(
            server, root, lambda: create_and_rename(root, payload, name))
        inspect_database(root, file_id, payload, name)
        run_server(server, root, lambda: read_after_restart(file_id, payload, name))
        with sqlite3.connect(root / "data" / "netdisk.db") as database:
            require(database.execute("SELECT COUNT(*) FROM files").fetchone()[0] == 0,
                    "delete left metadata row")
        run_server(server, root, lambda: verify_deleted(file_id))
    print("PASS: metadata persistence integration finished, 4 stages")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)

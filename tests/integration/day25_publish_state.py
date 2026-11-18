# ===== DAY 25 ADDED BEGIN 验证真实服务只公开ready对象 =====
import argparse
import json
from pathlib import Path
import re
import sqlite3
import sys
import tempfile

from day16_download_integration import exchange, require
from day19_file_mutation import run_server


def run_checks(root):
    database_path = root / "data" / "netdisk.db"
    objects = root / "data" / "objects"
    hidden_id = "upload-Hide25"
    hidden_payload = b"hidden-until-ready\x00"

    with sqlite3.connect(database_path) as database:
        database.execute(
            "INSERT INTO files(id, name, size, state, created_at) "
            "VALUES(?, ?, ?, 'publishing', ?)",
            (hidden_id, "hidden.bin", len(hidden_payload), 100),
        )
    (objects / hidden_id).write_bytes(hidden_payload)

    require(json.loads(exchange("GET", "/api/files")[2]) == {"files": []},
            "publishing row appeared in list")
    require(exchange("GET", f"/api/files/{hidden_id}")[0] == 404,
            "publishing row appeared in detail")
    require(exchange("GET", f"/api/files/{hidden_id}/content")[0] == 404,
            "publishing object became downloadable")
    print("PASS: publishing metadata and final object stay hidden from every read API")

    with sqlite3.connect(database_path) as database:
        changed = database.execute(
            "UPDATE files SET state = 'ready' "
            "WHERE id = ? AND state = 'publishing'", (hidden_id,)).rowcount
        require(changed == 1, "ready transition did not update one row")

    expected = {"id": hidden_id, "name": "hidden.bin", "size": len(hidden_payload)}
    require(json.loads(exchange("GET", "/api/files")[2]) == {"files": [expected]},
            "ready row missing from list")
    require(json.loads(exchange("GET", f"/api/files/{hidden_id}")[2]) == expected,
            "ready row missing from detail")
    require(exchange("GET", f"/api/files/{hidden_id}/content")[2] == hidden_payload,
            "ready object download differs")
    print("PASS: one committed state transition exposes list detail and content together")

    payload = b"normal-day25-upload"
    status, headers, body = exchange("POST", "/api/files", payload)
    file_id = headers.get("x-file-id", "")
    require(status == 201 and json.loads(body) == {"status": "created"} and
            re.fullmatch(r"upload-[A-Za-z0-9]{6}", file_id),
            "normal upload failed")
    with sqlite3.connect(database_path) as database:
        row = database.execute(
            "SELECT state, size FROM files WHERE id = ?", (file_id,)).fetchone()
        require(row == ("ready", len(payload)), "normal upload did not commit ready metadata")
        require(database.execute(
            "SELECT COUNT(*) FROM files WHERE state = 'publishing'").fetchone()[0] == 0,
            "normal upload left publishing metadata")
    require(exchange("GET", f"/api/files/{file_id}/content")[2] == payload,
            "normal ready upload is not downloadable")
    print("PASS: normal upload finishes in ready state without a publishing residue")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: publishing state integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    with tempfile.TemporaryDirectory(prefix="netdisk-day25-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        run_server(server, root, lambda: run_checks(root))
    print("PASS: publishing state integration finished, 3 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
# ===== DAY 25 ADDED END =====

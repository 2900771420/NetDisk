# ===== DAY 26 ADDED BEGIN 验证真实服务启动恢复publishing记录 =====
import argparse
import json
import os
from pathlib import Path
import sqlite3
import sys
import tempfile

from day16_download_integration import exchange, require
from day19_file_mutation import run_server


def initialize_schema():
    require(exchange("GET", "/health")[0] == 200, "initial service health failed")


def insert_publishing(database, file_id, payload, created_at):
    database.execute(
        "INSERT INTO files(id, name, size, state, created_at) "
        "VALUES(?, ?, ?, 'publishing', ?)",
        (file_id, f"{file_id}.bin", len(payload), created_at),
    )


def seed_interrupted_publications(root):
    uploads = root / "data" / "uploads"
    objects = root / "data" / "objects"
    fixtures = {
        "upload-Temp26": b"temporary-only\x00",
        "upload-Obj026": b"object-only\r\n",
        "upload-Both26": b"linked-twice",
        "upload-Miss26": b"",
    }
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        for created_at, (file_id, payload) in enumerate(fixtures.items(), start=100):
            insert_publishing(database, file_id, payload, created_at)
    (uploads / "upload-Temp26").write_bytes(fixtures["upload-Temp26"])
    (objects / "upload-Obj026").write_bytes(fixtures["upload-Obj026"])
    linked_temp = uploads / "upload-Both26"
    linked_temp.write_bytes(fixtures["upload-Both26"])
    os.link(linked_temp, objects / "upload-Both26")
    return fixtures


def verify_recovered(root, fixtures):
    uploads = root / "data" / "uploads"
    require(not list(uploads.iterdir()), "recovery left temporary names")
    for file_id in ("upload-Temp26", "upload-Obj026", "upload-Both26"):
        status, _, body = exchange("GET", f"/api/files/{file_id}/content")
        require(status == 200 and body == fixtures[file_id],
                f"recovered content differs for {file_id}")
    require(exchange("GET", "/api/files/upload-Miss26")[0] == 404,
            "missing publication became visible")
    listed = json.loads(exchange("GET", "/api/files")[2])["files"]
    require({item["id"] for item in listed} ==
            {"upload-Temp26", "upload-Obj026", "upload-Both26"},
            "recovered list differs")
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        rows = database.execute(
            "SELECT id, state FROM files ORDER BY id").fetchall()
        require(rows == [
            ("upload-Both26", "ready"),
            ("upload-Obj026", "ready"),
            ("upload-Temp26", "ready"),
        ], "recovered database states differ")
    print("PASS: startup completes three interrupted publications and cancels one missing pair")


def verify_idempotent(fixtures):
    for file_id in ("upload-Temp26", "upload-Obj026", "upload-Both26"):
        require(exchange("GET", f"/api/files/{file_id}/content")[2] == fixtures[file_id],
                "second restart changed recovered content")
    require(exchange("GET", "/health")[0] == 200,
            "service unhealthy after idempotent restart")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: publishing recovery integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    with tempfile.TemporaryDirectory(prefix="netdisk-day26-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        run_server(server, root, initialize_schema)
        fixtures = seed_interrupted_publications(root)

        run_server(server, root, lambda: verify_recovered(root, fixtures))
        log = (root / "server.log").read_text(encoding="utf-8", errors="replace")
        require("publishing recovery completed=3 cancelled=1" in log,
                "startup recovery summary differs")

        run_server(server, root, lambda: verify_idempotent(fixtures))
        log = (root / "server.log").read_text(encoding="utf-8", errors="replace")
        require("publishing recovery completed=0 cancelled=0" in log,
                "second startup was not idempotent")
        print("PASS: a second startup performs no recovery and preserves every ready object")
    print("PASS: publishing recovery integration finished, 2 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
# ===== DAY 26 ADDED END =====

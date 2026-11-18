# ===== DAY 29 ADDED BEGIN 验证真实分片上传摘要与schema迁移 =====
import argparse
import hashlib
from pathlib import Path
import re
import socket
import sqlite3
import sys
import tempfile

from day14_upload_integration import receive, upload_header, wait_for
from day16_download_integration import exchange, require
from day19_file_mutation import run_server


def seed_version_1(root):
    payload = b"legacy"
    file_id = "upload-Leg029"
    (root / "data" / "objects" / file_id).write_bytes(payload)
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        database.executescript("""
            CREATE TABLE schema_info (
                singleton INTEGER NOT NULL PRIMARY KEY CHECK (singleton = 1),
                version INTEGER NOT NULL CHECK (version = 1)
            );
            INSERT INTO schema_info(singleton, version) VALUES(1, 1);
            CREATE TABLE files (
                id TEXT NOT NULL PRIMARY KEY,
                name TEXT NOT NULL CHECK (length(CAST(name AS BLOB)) BETWEEN 1 AND 255),
                size INTEGER NOT NULL CHECK (size >= 0),
                state TEXT NOT NULL CHECK (state IN ('publishing', 'ready', 'deleting')),
                created_at INTEGER NOT NULL CHECK (created_at >= 0)
            );
            CREATE INDEX files_state_created_index
                ON files(state, created_at, id);
            INSERT INTO files(id, name, size, state, created_at)
            VALUES('upload-Leg029', 'legacy.bin', 6, 'ready', 100);
            PRAGMA user_version = 1;
        """)
    return file_id, payload


def verify_migration(root, legacy_id, legacy_payload):
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        require(database.execute("PRAGMA user_version").fetchone()[0] == 2,
                "schema user version was not migrated")
        require(database.execute(
            "SELECT version FROM schema_info WHERE singleton=1").fetchone()[0] == 2,
            "schema info version was not migrated")
        columns = {row[1] for row in database.execute("PRAGMA table_info(files)")}
        require("sha256" in columns, "sha256 column is missing after migration")
        require(database.execute(
            "SELECT sha256 FROM files WHERE id=?", (legacy_id,)).fetchone() == (None,),
            "legacy digest was fabricated")
    status, _, body = exchange("GET", f"/api/files/{legacy_id}/content")
    require(status == 200 and body == legacy_payload,
            "legacy object changed during migration")
    print("PASS: schema v1 migrated to v2 without inventing a legacy digest")


def upload_fragmented(root):
    payload = b"first\x00fragment\r\nsecond-fragment\xff"
    uploads = root / "data" / "uploads"
    objects = root / "data" / "objects"
    before = {path.name for path in objects.iterdir()}

    def wait_prefix(prefix):
        def written():
            require({path.name for path in objects.iterdir()} == before,
                    "partial upload became visible")
            paths = list(uploads.iterdir())
            return len(paths) == 1 and paths[0].read_bytes() == prefix
        wait_for(written, "server did not consume the expected upload fragment")

    with socket.create_connection(("127.0.0.1", 8080), timeout=5) as sock:
        sock.sendall(upload_header(len(payload)) + payload[:5])
        wait_prefix(payload[:5])
        sock.sendall(payload[5:17])
        wait_prefix(payload[:17])
        sock.sendall(payload[17:])
        status, headers, body = receive(sock)

    expected = hashlib.sha256(payload).hexdigest()
    file_id = headers.get("x-file-id", "")
    require(status == 201 and re.fullmatch(r"upload-[A-Za-z0-9]{6}", file_id),
            "fragmented upload failed")
    require(headers.get("x-file-sha256") == expected,
            "response SHA256 differs from client SHA256")
    require((objects / file_id).read_bytes() == payload,
            "published bytes differ from hashed bytes")
    require(not list(uploads.iterdir()), "temporary upload was not cleaned")
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        require(database.execute(
            "SELECT size, state, sha256 FROM files WHERE id=?", (file_id,)).fetchone() ==
                (len(payload), "ready", expected),
                "persisted SHA256 or publication state differs")
    print("PASS: three proven write fragments produce one persisted binary SHA256")
    return file_id, payload, expected


def upload_empty(root):
    status, headers, _ = exchange("POST", "/api/files", b"")
    expected = hashlib.sha256(b"").hexdigest()
    file_id = headers.get("x-file-id", "")
    require(status == 201 and headers.get("x-file-sha256") == expected,
            "empty upload SHA256 differs")
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        require(database.execute(
            "SELECT sha256 FROM files WHERE id=?", (file_id,)).fetchone() == (expected,),
            "empty SHA256 was not persisted")
    print("PASS: empty upload uses the standard SHA256 value")


def verify_restart(root, file_id, payload, expected):
    status, _, body = exchange("GET", f"/api/files/{file_id}/content")
    require(status == 200 and body == payload,
            "hashed object changed after restart")
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        require(database.execute(
            "SELECT sha256 FROM files WHERE id=? AND state='ready'", (file_id,)).fetchone() ==
                (expected,), "SHA256 changed after restart")
    print("PASS: restart preserves the ready object and its SHA256 metadata")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: SHA256 integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")

    with tempfile.TemporaryDirectory(prefix="netdisk-day29-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        legacy_id, legacy_payload = seed_version_1(root)

        def first_run():
            verify_migration(root, legacy_id, legacy_payload)
            result = upload_fragmented(root)
            upload_empty(root)
            return result

        file_id, payload, expected = run_server(server, root, first_run)
        run_server(server, root,
                   lambda: verify_restart(root, file_id, payload, expected))

    print("PASS: SHA256 integration finished 4 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
# ===== DAY 29 ADDED END =====

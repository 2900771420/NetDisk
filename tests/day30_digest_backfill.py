# ===== DAY 30 ADDED BEGIN 验证真实启动回填与GET HEAD强ETag =====
import argparse
import hashlib
from pathlib import Path
import sqlite3
import sys
import tempfile

from day16_download_integration import exchange, require
from day19_file_mutation import patch, run_server


def seed_version_2(root):
    legacy_id = "upload-Leg030"
    legacy_payload = b"legacy\x00day30"
    existing_id = "upload-New030"
    existing_payload = b"already hashed"
    missing_id = "upload-Mis030"
    legacy_digest = hashlib.sha256(legacy_payload).hexdigest()
    existing_digest = hashlib.sha256(existing_payload).hexdigest()

    objects = root / "data" / "objects"
    (objects / legacy_id).write_bytes(legacy_payload)
    (objects / existing_id).write_bytes(existing_payload)
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        database.executescript("""
            CREATE TABLE schema_info (
                singleton INTEGER NOT NULL PRIMARY KEY CHECK (singleton = 1),
                version INTEGER NOT NULL CHECK (version = 2)
            );
            INSERT INTO schema_info(singleton, version) VALUES(1, 2);
            CREATE TABLE files (
                id TEXT NOT NULL PRIMARY KEY,
                name TEXT NOT NULL CHECK (length(CAST(name AS BLOB)) BETWEEN 1 AND 255),
                size INTEGER NOT NULL CHECK (size >= 0),
                state TEXT NOT NULL CHECK (state IN ('publishing', 'ready', 'deleting')),
                created_at INTEGER NOT NULL CHECK (created_at >= 0),
                sha256 TEXT CHECK (sha256 IS NULL OR
                    (length(sha256) = 64 AND sha256 NOT GLOB '*[^0-9a-f]*'))
            );
            CREATE INDEX files_state_created_index
                ON files(state, created_at, id);
            PRAGMA user_version = 2;
        """)
        database.executemany(
            "INSERT INTO files(id,name,size,state,created_at,sha256) "
            "VALUES(?,?,?,'ready',?,?)",
            [
                (legacy_id, "legacy.bin", len(legacy_payload), 100, None),
                (existing_id, "existing.bin", len(existing_payload), 101,
                 existing_digest),
                (missing_id, "missing.bin", 7, 102, None),
            ])
    return (legacy_id, legacy_payload, legacy_digest,
            existing_id, existing_payload, existing_digest, missing_id)


def verify_first_start(root, fixture):
    (legacy_id, legacy_payload, legacy_digest,
     existing_id, existing_payload, existing_digest, missing_id) = fixture
    log = (root / "server.log").read_text(encoding="utf-8", errors="replace")
    require("digest backfill ready=3 already_hashed=1 backfilled=1 skipped=1" in log,
            "first startup backfill summary differs")

    status, get_headers, body = exchange("GET", f"/api/files/{legacy_id}/content")
    require(status == 200 and body == legacy_payload and
            get_headers.get("etag") == f'"{legacy_digest}"',
            "legacy download digest or ETag differs")
    status, head_headers, head_body = exchange(
        "HEAD", f"/api/files/{legacy_id}/content")
    require(status == 200 and not head_body and head_headers == get_headers,
            "legacy HEAD does not retain the GET ETag")

    require(patch(f"/api/files/{legacy_id}", {"name": "renamed.bin"})[0] == 200,
            "rename failed")
    status, renamed_headers, renamed_body = exchange(
        "GET", f"/api/files/{legacy_id}/content")
    require(status == 200 and renamed_body == legacy_payload and
            renamed_headers.get("etag") == f'"{legacy_digest}"',
            "metadata rename changed the immutable content ETag")

    status, headers, body = exchange("GET", f"/api/files/{existing_id}/content")
    require(status == 200 and body == existing_payload and
            headers.get("etag") == f'"{existing_digest}"',
            "existing digest was not used as ETag")
    require(exchange("GET", f"/api/files/{missing_id}/content")[0] == 404,
            "missing legacy object became downloadable")

    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        rows = dict(database.execute("SELECT id,sha256 FROM files"))
    require(rows[legacy_id] == legacy_digest and
            rows[existing_id] == existing_digest and rows[missing_id] is None,
            "backfill overwrote or fabricated a digest")
    print("PASS: startup backfills one healthy legacy object and serves strong ETags")


def verify_second_start(root, fixture):
    legacy_id, legacy_payload, legacy_digest, _, _, _, missing_id = fixture
    log = (root / "server.log").read_text(encoding="utf-8", errors="replace")
    require("digest backfill ready=3 already_hashed=2 backfilled=0 skipped=1" in log,
            "second startup was not idempotent")
    status, headers, body = exchange("GET", f"/api/files/{legacy_id}/content")
    require(status == 200 and body == legacy_payload and
            headers.get("etag") == f'"{legacy_digest}"',
            "restart changed the legacy ETag")
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        require(database.execute(
            "SELECT sha256 FROM files WHERE id=?", (missing_id,)).fetchone() == (None,),
            "restart fabricated a digest for a missing object")
    print("PASS: restart skips known digests and preserves unresolved NULL values")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: digest backfill integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")

    with tempfile.TemporaryDirectory(prefix="netdisk-day30-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        fixture = seed_version_2(root)
        run_server(server, root, lambda: verify_first_start(root, fixture))
        run_server(server, root, lambda: verify_second_start(root, fixture))

    print("PASS: digest backfill integration finished 2 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
# ===== DAY 30 ADDED END =====

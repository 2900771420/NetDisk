# ===== DAY 27 ADDED BEGIN 验证真实服务恢复deleting并保护ready对象 =====
import argparse
from pathlib import Path
import sqlite3
import sys
import tempfile

from day16_download_integration import exchange, require
from day19_file_mutation import delete, run_server


def initialize_schema():
    require(exchange("GET", "/health")[0] == 200,
            "initial service health failed")


def seed_interrupted_deletions(root):
    fixtures = {
        "upload-DelO27": b"remove after state commit",
        "upload-DelM27": b"object already removed",
        "upload-Keep27": b"ready object must survive",
    }
    objects = root / "data" / "objects"
    (objects / "upload-DelO27").write_bytes(fixtures["upload-DelO27"])
    (objects / "upload-Keep27").write_bytes(fixtures["upload-Keep27"])
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        for created_at, (file_id, payload) in enumerate(fixtures.items(), start=200):
            state = "ready" if file_id == "upload-Keep27" else "deleting"
            database.execute(
                "INSERT INTO files(id, name, size, state, created_at) "
                "VALUES(?, ?, ?, ?, ?)",
                (file_id, f"{file_id}.bin", len(payload), state, created_at),
            )
    return fixtures


def verify_recovered(root, fixtures):
    objects = root / "data" / "objects"
    for file_id in ("upload-DelO27", "upload-DelM27"):
        require(exchange("GET", f"/api/files/{file_id}")[0] == 404,
                f"deleting metadata became visible for {file_id}")
        require(not (objects / file_id).exists(),
                f"deleting object remains for {file_id}")
    status, _, body = exchange("GET", "/api/files/upload-Keep27/content")
    require(status == 200 and body == fixtures["upload-Keep27"],
            "ready object was removed or changed")
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        rows = database.execute(
            "SELECT id, state FROM files ORDER BY id").fetchall()
        require(rows == [("upload-Keep27", "ready")],
                "recovered database rows differ")
    print("PASS: startup finishes object-present and object-missing deletions")


def verify_idempotent_and_delete_ready(root, payload):
    require(exchange("GET", "/api/files/upload-Keep27/content")[2] == payload,
            "second restart changed ready content")
    require(delete("/api/files/upload-Keep27")[0] == 204,
            "normal DELETE did not complete")
    require(not (root / "data" / "objects" / "upload-Keep27").exists(),
            "normal DELETE left the object")
    require(exchange("GET", "/api/files/upload-Keep27")[0] == 404,
            "normal DELETE left public metadata")
    print("PASS: idempotent restart preserves ready data and normal DELETE commits all phases")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: deletion recovery integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    with tempfile.TemporaryDirectory(prefix="netdisk-day27-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        run_server(server, root, initialize_schema)
        fixtures = seed_interrupted_deletions(root)

        run_server(server, root, lambda: verify_recovered(root, fixtures))
        log = (root / "server.log").read_text(encoding="utf-8", errors="replace")
        require("deletion recovery completed=2 objects_removed=1" in log,
                "startup deletion recovery summary differs")

        run_server(server, root, lambda: verify_idempotent_and_delete_ready(
            root, fixtures["upload-Keep27"]))
        log = (root / "server.log").read_text(encoding="utf-8", errors="replace")
        require("deletion recovery completed=0 objects_removed=0" in log,
                "second startup was not idempotent")

        run_server(server, root, lambda: require(
            exchange("GET", "/health")[0] == 200,
            "service unhealthy after completed deletion"))
        with sqlite3.connect(root / "data" / "netdisk.db") as database:
            require(database.execute("SELECT COUNT(*) FROM files").fetchone()[0] == 0,
                    "completed deletion returned after restart")
        print("PASS: completed deletion remains absent after another restart")
    print("PASS: deletion recovery integration finished, 3 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
# ===== DAY 27 ADDED END =====

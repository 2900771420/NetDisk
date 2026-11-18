# ===== DAY 28 ADDED BEGIN 验证真实服务报告异常但不自动删除 =====
import argparse
from pathlib import Path
import sqlite3
import sys
import tempfile

from day16_download_integration import exchange, require
from day19_file_mutation import run_server


def initialize_schema():
    require(exchange("GET", "/health")[0] == 200,
            "initial service health failed")


def seed_anomalies(root):
    fixtures = {
        "upload-Hea028": b"healthy",
        "upload-Mis028": b"missing",
        "upload-Siz028": b"1234",
        "upload-Typ028": b"",
        "invalid-id": b"",
    }
    objects = root / "data" / "objects"
    (objects / "upload-Hea028").write_bytes(fixtures["upload-Hea028"])
    (objects / "upload-Siz028").write_bytes(b"xx")
    (objects / "upload-Typ028").mkdir()
    (objects / "upload-Orp028").write_bytes(b"orphan")
    (objects / "manual-dir").mkdir()
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        for created_at, (file_id, payload) in enumerate(fixtures.items(), start=300):
            database.execute(
                "INSERT INTO files(id, name, size, state, created_at) "
                "VALUES(?, ?, ?, 'ready', ?)",
                (file_id, f"{file_id}.bin", len(payload), created_at),
            )
    return fixtures


def verify_service_and_evidence(root, fixtures):
    require(exchange("GET", "/health")[0] == 200,
            "service did not listen after audit findings")
    status, _, body = exchange("GET", "/api/files/upload-Hea028/content")
    require(status == 200 and body == fixtures["upload-Hea028"],
            "healthy object was unavailable")
    require(exchange("GET", "/api/files/upload-Mis028/content")[0] == 404,
            "missing object response differs")
    require(exchange("GET", "/api/files/upload-Siz028/content")[0] == 500,
            "size mismatch response differs")
    require(exchange("GET", "/api/files/upload-Typ028/content")[0] == 404,
            "non-regular object response differs")

    objects = root / "data" / "objects"
    require((objects / "upload-Orp028").read_bytes() == b"orphan" and
            (objects / "manual-dir").is_dir() and
            (objects / "upload-Typ028").is_dir() and
            (objects / "upload-Siz028").read_bytes() == b"xx",
            "audit changed disk evidence")
    with sqlite3.connect(root / "data" / "netdisk.db") as database:
        require(database.execute(
            "SELECT COUNT(*) FROM files WHERE state='ready'").fetchone()[0] == 5,
            "audit changed ready metadata")


def verify_log(root):
    log = (root / "server.log").read_text(encoding="utf-8", errors="replace")
    require("storage audit ready=5 disk_entries=5 healthy=1 findings=6" in log,
            "audit summary differs")
    for kind in ("invalid_metadata_id", "missing_object", "size_mismatch",
                 "non_regular_object", "orphan_object", "unexpected_entry"):
        require(f"storage audit finding kind={kind}" in log,
                f"missing audit finding {kind}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: storage audit integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    with tempfile.TemporaryDirectory(prefix="netdisk-day28-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        run_server(server, root, initialize_schema)
        fixtures = seed_anomalies(root)

        run_server(server, root, lambda: verify_service_and_evidence(root, fixtures))
        verify_log(root)
        print("PASS: startup reports six anomaly classes and keeps healthy data available")

        run_server(server, root, lambda: verify_service_and_evidence(root, fixtures))
        verify_log(root)
        print("PASS: repeated audit reports the same evidence without deleting or repairing it")
    print("PASS: storage audit integration finished, 2 groups")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
# ===== DAY 28 ADDED END =====

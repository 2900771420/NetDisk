// ===== DAY 28 ADDED BEGIN 验证只读审计分类 全量分页与异常保留 =====
#include "metadata_database.h"
#include "storage_audit.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

struct TestDirectory
{
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("day28-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    TestDirectory()
    {
        require(std::filesystem::create_directory(root), "create isolated directory");
        std::filesystem::current_path(root);
    }

    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::current_path(previous, error);
        std::filesystem::remove_all(root, error);
    }
};

std::filesystem::path object_path(std::string_view id)
{
    return std::filesystem::path("data/objects") / id;
}

void write_file(const std::filesystem::path& path, std::string_view bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(file.good(), "write object fixture");
}

void create_ready(FileMetadataStore& store, std::string_view id,
                  std::string_view payload, std::int64_t created_at,
                  bool create_object = true)
{
    if (create_object) write_file(object_path(id), payload);
    require(store.create_publishing(id, std::string(id) + ".bin",
                payload.size(), created_at) == MetadataResult::Ok &&
            store.mark_ready(id) == MetadataResult::Ok,
            "create ready fixture");
}

bool contains(const StorageAuditReport& report, StorageAuditFindingKind kind,
              std::string_view id)
{
    for (const auto& finding : report.findings)
        if (finding.kind == kind && finding.id == id) return true;
    return false;
}

void verify_report(const StorageAuditReport& report)
{
    require(report.ready_records == 110, "ready row count differs");
    require(report.disk_entries == 110, "disk entry count differs");
    require(report.healthy_objects == 106, "healthy object count differs");
    require(report.invalid_metadata_ids == 1 && report.missing_objects == 1 &&
                report.size_mismatches == 1 && report.non_regular_objects == 1 &&
                report.orphan_objects == 1 && report.unexpected_entries == 1 &&
                report.findings.size() == 6 && !report.clean(),
            "finding summary differs");
    require(contains(report, StorageAuditFindingKind::InvalidMetadataId, "invalid-id") &&
                contains(report, StorageAuditFindingKind::MissingObject, "upload-Mis028") &&
                contains(report, StorageAuditFindingKind::SizeMismatch, "upload-Siz028") &&
                contains(report, StorageAuditFindingKind::NonRegularObject, "upload-Typ028") &&
                contains(report, StorageAuditFindingKind::OrphanObject, "upload-Orp028") &&
                contains(report, StorageAuditFindingKind::UnexpectedEntry, "manual-dir"),
            "finding classification differs");
}

int main()
{
    try
    {
        TestDirectory directory;
        std::filesystem::create_directories("data/objects");
        MetadataDatabase database;
        require(database.open("data/netdisk.db") && database.initialize_schema(),
                "initialize metadata database");
        FileMetadataStore store(database.get());
        require(store.prepare(), "prepare metadata statements");

        for (int index = 0; index < 105; ++index)
        {
            std::string suffix = std::to_string(index);
            suffix.insert(suffix.begin(), 5 - suffix.size(), '0');
            create_ready(store, "upload-P" + suffix, "page", 1000 + index);
        }
        create_ready(store, "upload-Hea028", "healthy", 2000);
        create_ready(store, "upload-Mis028", "missing", 2001, false);
        create_ready(store, "upload-Siz028", "1234", 2002, false);
        write_file(object_path("upload-Siz028"), "xx");
        create_ready(store, "upload-Typ028", "", 2003, false);
        std::filesystem::create_directory(object_path("upload-Typ028"));
        create_ready(store, "invalid-id", "", 2004, false);
        write_file(object_path("upload-Orp028"), "orphan");
        std::filesystem::create_directory(object_path("manual-dir"));

        StorageAuditor auditor(store);
        StorageAuditReport report;
        require(auditor.run(report), "storage audit failed");
        verify_report(report);

        FileMetadata metadata;
        require(store.find_ready("upload-Mis028", metadata) == MetadataResult::Ok &&
                    store.find_ready("upload-Siz028", metadata) == MetadataResult::Ok &&
                    std::filesystem::is_regular_file(object_path("upload-Orp028")) &&
                    std::filesystem::is_directory(object_path("manual-dir")),
                "audit changed metadata or disk evidence");
        require(auditor.run(report), "repeated storage audit failed");
        verify_report(report);
        require(escape_storage_audit_value("line\nbreak") == "line%0Abreak",
                "audit log escaping differs");

        std::filesystem::remove_all("data/objects");
        write_file("data/objects", "not a directory");
        require(!auditor.run(report) && !auditor.error_message().empty(),
                "incomplete directory audit did not fail");

        std::cout << "PASS: storage audit classifies six anomalies without changing data\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
// ===== DAY 28 ADDED END =====

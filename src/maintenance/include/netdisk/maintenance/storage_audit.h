#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

class FileMetadataStore;
struct FileMetadata;

// ===== DAY 33A ADDED BEGIN 提取只读存储审计稳定接口 =====
enum class StorageAuditFindingKind
{
    InvalidMetadataId,
    MissingObject,
    SizeMismatch,
    NonRegularObject,
    OrphanObject,
    UnexpectedEntry
};

struct StorageAuditFinding
{
    StorageAuditFindingKind kind = StorageAuditFindingKind::MissingObject;
    std::string id;
    std::uint64_t expected_size = 0;
    std::uintmax_t actual_size = 0;
};

struct StorageAuditReport
{
    std::size_t ready_records = 0;
    std::size_t disk_entries = 0;
    std::size_t healthy_objects = 0;
    std::size_t invalid_metadata_ids = 0;
    std::size_t missing_objects = 0;
    std::size_t size_mismatches = 0;
    std::size_t non_regular_objects = 0;
    std::size_t orphan_objects = 0;
    std::size_t unexpected_entries = 0;
    std::vector<StorageAuditFinding> findings;

    bool clean() const noexcept;
};

const char* storage_audit_kind_name(StorageAuditFindingKind kind) noexcept;
std::string escape_storage_audit_value(std::string_view value);

class StorageAuditor
{
public:
    explicit StorageAuditor(FileMetadataStore& store) noexcept;
    bool run(StorageAuditReport& report);
    const std::string& error_message() const noexcept;

private:
    void add_finding(StorageAuditReport& report, StorageAuditFindingKind kind,
                     std::string id, std::uint64_t expected_size = 0,
                     std::uintmax_t actual_size = 0);
    bool audit_ready(const FileMetadata& file, StorageAuditReport& report);
    bool audit_directory(const std::unordered_set<std::string>& ready_ids,
                         StorageAuditReport& report);
    bool fail(std::string operation, std::string_view detail);

    FileMetadataStore& store_;
    std::string error_message_;
};
// ===== DAY 33A ADDED END =====

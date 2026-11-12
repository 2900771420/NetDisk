// ===== DAY 28 ADDED BEGIN 只读审计ready元数据与正式对象目录 =====
#pragma once

#include "file_metadata_store.h"
#include "object_path.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

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

    bool clean() const noexcept
    {
        return findings.empty();
    }
};

inline const char* storage_audit_kind_name(StorageAuditFindingKind kind) noexcept
{
    switch (kind)
    {
        case StorageAuditFindingKind::InvalidMetadataId:
            return "invalid_metadata_id";
        case StorageAuditFindingKind::MissingObject:
            return "missing_object";
        case StorageAuditFindingKind::SizeMismatch:
            return "size_mismatch";
        case StorageAuditFindingKind::NonRegularObject:
            return "non_regular_object";
        case StorageAuditFindingKind::OrphanObject:
            return "orphan_object";
        case StorageAuditFindingKind::UnexpectedEntry:
            return "unexpected_entry";
    }
    return "unknown";
}

inline std::string escape_storage_audit_value(std::string_view value)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(value.size());
    for (unsigned char c : value)
    {
        const bool safe = (c >= 'a' && c <= 'z') ||
                          (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') ||
                          c == '-' || c == '_' || c == '.';
        if (safe)
        {
            escaped.push_back(static_cast<char>(c));
            continue;
        }
        escaped.push_back('%');
        escaped.push_back(hex[c >> 4]);
        escaped.push_back(hex[c & 0x0f]);
    }
    return escaped;
}

class StorageAuditor
{
public:
    explicit StorageAuditor(FileMetadataStore& store) noexcept
        : store_(store)
    {
    }

    bool run(StorageAuditReport& report)
    {
        report = {};
        error_message_.clear();

        std::unordered_set<std::string> ready_ids;
        FilePageRequest page;
        page.limit = 100;
        while (true)
        {
            std::vector<FileMetadata> files;
            bool has_more = false;
            if (store_.list_ready(page, files, has_more) != MetadataResult::Ok)
                return fail("list ready metadata", store_.error_message());

            for (const FileMetadata& file : files)
            {
                ++report.ready_records;
                ready_ids.insert(file.id);
                if (!audit_ready(file, report)) return false;
            }
            if (!has_more) break;
            if (files.empty())
                return fail("list ready metadata", "pagination returned no cursor row");
            page.has_cursor = true;
            page.cursor_created_at = files.back().created_at;
            page.cursor_id = files.back().id;
        }

        return audit_directory(ready_ids, report);
    }

    const std::string& error_message() const noexcept
    {
        return error_message_;
    }

private:
    void add_finding(StorageAuditReport& report, StorageAuditFindingKind kind,
                     std::string id, std::uint64_t expected_size = 0,
                     std::uintmax_t actual_size = 0)
    {
        report.findings.push_back(
            StorageAuditFinding{kind, std::move(id), expected_size, actual_size});
        switch (kind)
        {
            case StorageAuditFindingKind::InvalidMetadataId:
                ++report.invalid_metadata_ids;
                break;
            case StorageAuditFindingKind::MissingObject:
                ++report.missing_objects;
                break;
            case StorageAuditFindingKind::SizeMismatch:
                ++report.size_mismatches;
                break;
            case StorageAuditFindingKind::NonRegularObject:
                ++report.non_regular_objects;
                break;
            case StorageAuditFindingKind::OrphanObject:
                ++report.orphan_objects;
                break;
            case StorageAuditFindingKind::UnexpectedEntry:
                ++report.unexpected_entries;
                break;
        }
    }

    bool audit_ready(const FileMetadata& file, StorageAuditReport& report)
    {
        std::string object_text;
        if (!make_object_path(file.id, object_text))
        {
            add_finding(report, StorageAuditFindingKind::InvalidMetadataId,
                        file.id, file.size);
            return true;
        }

        const std::filesystem::path object(object_text);
        std::error_code status_error;
        const auto status = std::filesystem::symlink_status(object, status_error);
        if (status_error)
        {
            if (status_error == std::errc::no_such_file_or_directory)
            {
                add_finding(report, StorageAuditFindingKind::MissingObject,
                            file.id, file.size);
                return true;
            }
            return fail("inspect ready object id=" + escape_storage_audit_value(file.id),
                        status_error.message());
        }
        if (!std::filesystem::exists(status))
        {
            add_finding(report, StorageAuditFindingKind::MissingObject,
                        file.id, file.size);
            return true;
        }
        if (!std::filesystem::is_regular_file(status))
        {
            add_finding(report, StorageAuditFindingKind::NonRegularObject,
                        file.id, file.size);
            return true;
        }

        std::error_code size_error;
        const std::uintmax_t actual_size =
            std::filesystem::file_size(object, size_error);
        if (size_error)
            return fail("read ready object size id=" + escape_storage_audit_value(file.id),
                        size_error.message());
        if (actual_size != file.size)
        {
            add_finding(report, StorageAuditFindingKind::SizeMismatch,
                        file.id, file.size, actual_size);
            return true;
        }

        ++report.healthy_objects;
        return true;
    }

    bool audit_directory(const std::unordered_set<std::string>& ready_ids,
                         StorageAuditReport& report)
    {
        const std::filesystem::path root("data/objects");
        std::error_code root_error;
        const auto root_status = std::filesystem::symlink_status(root, root_error);
        if (root_error)
        {
            if (root_error == std::errc::no_such_file_or_directory) return true;
            return fail("inspect object directory", root_error.message());
        }
        if (!std::filesystem::exists(root_status)) return true;
        if (!std::filesystem::is_directory(root_status))
            return fail("inspect object directory", "path is not a directory");

        std::vector<std::filesystem::path> entries;
        std::error_code iterate_error;
        std::filesystem::directory_iterator iterator(root, iterate_error);
        const std::filesystem::directory_iterator end;
        if (iterate_error)
            return fail("open object directory", iterate_error.message());
        while (iterator != end)
        {
            entries.push_back(iterator->path());
            iterator.increment(iterate_error);
            if (iterate_error)
                return fail("iterate object directory", iterate_error.message());
        }
        std::sort(entries.begin(), entries.end());

        for (const auto& entry : entries)
        {
            ++report.disk_entries;
            const std::string name = entry.filename().string();
            if (ready_ids.find(name) != ready_ids.end()) continue;

            std::error_code status_error;
            const auto status = std::filesystem::symlink_status(entry, status_error);
            if (status_error)
                return fail("inspect object entry name=" +
                                escape_storage_audit_value(name),
                            status_error.message());
            add_finding(report,
                        std::filesystem::is_regular_file(status)
                            ? StorageAuditFindingKind::OrphanObject
                            : StorageAuditFindingKind::UnexpectedEntry,
                        name);
        }
        return true;
    }

    bool fail(std::string operation, std::string_view detail)
    {
        error_message_ = std::move(operation);
        error_message_ += ": ";
        error_message_.append(detail);
        return false;
    }

    FileMetadataStore& store_;
    std::string error_message_;
};
// ===== DAY 28 ADDED END =====

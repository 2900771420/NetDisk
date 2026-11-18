#include <netdisk/maintenance/recovery.h>

#include <netdisk/metadata/file_metadata_store.h>
#include <netdisk/storage/object_path.h>

#include <filesystem>
#include <system_error>
#include <vector>

namespace
{
// ===== DAY 33A ADDED BEGIN 恢复实现共享磁盘检查助手 =====
enum class EntryKind
{
    Missing,
    Regular,
    Invalid
};

struct DiskEntry
{
    EntryKind kind = EntryKind::Missing;
    std::uintmax_t size = 0;
};

bool inspect_entry(const std::filesystem::path& path, DiskEntry& entry,
                   std::string& detail)
{
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(path, error);
    if (error)
    {
        if (error == std::errc::no_such_file_or_directory)
        {
            entry = {};
            return true;
        }
        detail = error.message();
        return false;
    }
    if (!std::filesystem::exists(status))
    {
        entry = {};
        return true;
    }
    if (!std::filesystem::is_regular_file(status))
    {
        entry.kind = EntryKind::Invalid;
        entry.size = 0;
        return true;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
    {
        detail = error.message();
        return false;
    }
    entry.kind = EntryKind::Regular;
    entry.size = size;
    return true;
}
// ===== DAY 33A ADDED END =====
}

// ===== DAY 33A ADDED BEGIN 将发布恢复实现移出头文件 =====
PublishingRecovery::PublishingRecovery(FileMetadataStore& store) noexcept
    : store_(store)
{
}

bool PublishingRecovery::run(PublishingRecoveryReport& report)
{
    report = {};
    error_message_.clear();
    std::vector<FileMetadata> publishing;
    if (store_.list_publishing(publishing) != MetadataResult::Ok)
        return fail("list publishing metadata", "", store_.error_message());
    for (const FileMetadata& file : publishing)
        if (!recover_one(file, report)) return false;
    return true;
}

const std::string& PublishingRecovery::error_message() const noexcept
{
    return error_message_;
}

bool PublishingRecovery::recover_one(const FileMetadata& file,
                                     PublishingRecoveryReport& report)
{
    std::string object_text;
    if (!make_object_path(file.id, object_text))
        return fail("validate publishing id", file.id, "invalid object id");

    const std::filesystem::path temporary =
        std::filesystem::path("data/uploads") / file.id;
    const std::filesystem::path object(object_text);
    DiskEntry temporary_entry;
    DiskEntry object_entry;
    std::string detail;
    if (!inspect_entry(temporary, temporary_entry, detail))
        return fail("inspect temporary file", file.id, detail);
    if (!inspect_entry(object, object_entry, detail))
        return fail("inspect object file", file.id, detail);

    if (temporary_entry.kind == EntryKind::Invalid)
        return fail("inspect temporary file", file.id, "entry is not a regular file");
    if (object_entry.kind == EntryKind::Invalid)
        return fail("inspect object file", file.id, "entry is not a regular file");
    if (temporary_entry.kind == EntryKind::Regular &&
        temporary_entry.size != file.size)
        return fail("verify temporary file", file.id, "size differs from metadata");
    if (object_entry.kind == EntryKind::Regular && object_entry.size != file.size)
        return fail("verify object file", file.id, "size differs from metadata");

    if (object_entry.kind == EntryKind::Regular)
    {
        if (temporary_entry.kind == EntryKind::Regular)
        {
            std::error_code equivalent_error;
            const bool same = std::filesystem::equivalent(
                temporary, object, equivalent_error);
            if (equivalent_error)
                return fail("compare publishing links", file.id,
                            equivalent_error.message());
            if (!same)
                return fail("compare publishing links", file.id,
                            "temporary and object names refer to different files");
            std::error_code remove_error;
            const bool removed = std::filesystem::remove(temporary, remove_error);
            if (remove_error)
                return fail("remove recovered temporary file", file.id,
                            remove_error.message());
            if (!removed)
                return fail("remove recovered temporary file", file.id,
                            "temporary file disappeared");
        }
        return mark_ready(file.id, report);
    }

    if (temporary_entry.kind == EntryKind::Regular)
    {
        std::error_code link_error;
        std::filesystem::create_hard_link(temporary, object, link_error);
        if (link_error)
            return fail("link recovered object", file.id, link_error.message());
        std::error_code remove_error;
        const bool removed = std::filesystem::remove(temporary, remove_error);
        if (remove_error)
            return fail("remove recovered temporary file", file.id,
                        remove_error.message());
        if (!removed)
            return fail("remove recovered temporary file", file.id,
                        "temporary file disappeared");
        return mark_ready(file.id, report);
    }

    const MetadataResult cancelled = store_.cancel_publishing(file.id);
    if (cancelled != MetadataResult::Ok)
        return fail("cancel missing publication", file.id,
                    cancelled == MetadataResult::NotFound
                        ? "publishing row disappeared" : store_.error_message());
    ++report.cancelled;
    return true;
}

bool PublishingRecovery::mark_ready(std::string_view id,
                                    PublishingRecoveryReport& report)
{
    const MetadataResult ready = store_.mark_ready(id);
    if (ready != MetadataResult::Ok)
        return fail("mark recovered object ready", id,
                    ready == MetadataResult::NotFound
                        ? "publishing row disappeared" : store_.error_message());
    ++report.completed;
    return true;
}

bool PublishingRecovery::fail(std::string_view operation, std::string_view id,
                              std::string_view detail)
{
    error_message_.assign(operation);
    if (!id.empty())
    {
        error_message_ += " id=";
        error_message_.append(id);
    }
    error_message_ += ": ";
    error_message_.append(detail);
    return false;
}
// ===== DAY 33A ADDED END =====

// ===== DAY 33A ADDED BEGIN 将删除恢复实现移出头文件 =====
DeletionRecovery::DeletionRecovery(FileMetadataStore& store) noexcept
    : store_(store)
{
}

bool DeletionRecovery::run(DeletionRecoveryReport& report)
{
    report = {};
    error_message_.clear();
    std::vector<FileMetadata> deleting;
    if (store_.list_deleting(deleting) != MetadataResult::Ok)
        return fail("list deleting metadata", "", store_.error_message());
    for (const FileMetadata& file : deleting)
        if (!recover_one(file, report)) return false;
    return true;
}

const std::string& DeletionRecovery::error_message() const noexcept
{
    return error_message_;
}

bool DeletionRecovery::recover_one(const FileMetadata& file,
                                   DeletionRecoveryReport& report)
{
    std::string object_text;
    if (!make_object_path(file.id, object_text))
        return fail("validate deleting id", file.id, "invalid object id");

    const std::filesystem::path object(object_text);
    std::error_code status_error;
    const auto status = std::filesystem::symlink_status(object, status_error);
    if (status_error && status_error != std::errc::no_such_file_or_directory)
        return fail("inspect deleting object", file.id, status_error.message());

    bool removed = false;
    if (!status_error && std::filesystem::exists(status))
    {
        if (!std::filesystem::is_regular_file(status))
            return fail("inspect deleting object", file.id,
                        "entry is not a regular file");
        std::error_code size_error;
        const std::uintmax_t size = std::filesystem::file_size(object, size_error);
        if (size_error)
            return fail("read deleting object size", file.id, size_error.message());
        if (size != file.size)
            return fail("verify deleting object", file.id,
                        "size differs from metadata");
        std::error_code remove_error;
        removed = std::filesystem::remove(object, remove_error);
        if (remove_error)
            return fail("remove deleting object", file.id, remove_error.message());
    }

    const MetadataResult erased = store_.erase_deleting(file.id);
    if (erased != MetadataResult::Ok)
        return fail("erase deleting metadata", file.id,
                    erased == MetadataResult::NotFound
                        ? "deleting row disappeared" : store_.error_message());
    ++report.completed;
    if (removed) ++report.objects_removed;
    return true;
}

bool DeletionRecovery::fail(std::string_view operation, std::string_view id,
                            std::string_view detail)
{
    error_message_.assign(operation);
    if (!id.empty())
    {
        error_message_ += " id=";
        error_message_.append(id);
    }
    error_message_ += ": ";
    error_message_.append(detail);
    return false;
}
// ===== DAY 33A ADDED END =====

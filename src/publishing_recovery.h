// ===== DAY 26 ADDED BEGIN 启动时幂等恢复publishing对象 =====
#pragma once

#include "file_metadata_store.h"
#include "object_path.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

struct PublishingRecoveryReport
{
    std::size_t completed = 0;
    std::size_t cancelled = 0;
};

class PublishingRecovery
{
public:
    explicit PublishingRecovery(FileMetadataStore& store) noexcept
        : store_(store)
    {
    }

    bool run(PublishingRecoveryReport& report)
    {
        report = {};
        error_message_.clear();

        std::vector<FileMetadata> publishing;
        if (store_.list_publishing(publishing) != MetadataResult::Ok)
            return fail("list publishing metadata", "", store_.error_message());

        for (const FileMetadata& file : publishing)
        {
            if (!recover_one(file, report)) return false;
        }
        return true;
    }

    const std::string& error_message() const noexcept
    {
        return error_message_;
    }

private:
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

    bool inspect(const std::filesystem::path& path, DiskEntry& entry,
                 std::string_view id, const char* label)
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
            return fail(label, id, error.message());
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
        if (error) return fail(label, id, error.message());
        entry.kind = EntryKind::Regular;
        entry.size = size;
        return true;
    }

    bool recover_one(const FileMetadata& file, PublishingRecoveryReport& report)
    {
        std::string object_text;
        if (!make_object_path(file.id, object_text))
            return fail("validate publishing id", file.id, "invalid object id");

        const std::filesystem::path temporary =
            std::filesystem::path("data/uploads") / file.id;
        const std::filesystem::path object(object_text);
        DiskEntry temporary_entry;
        DiskEntry object_entry;
        if (!inspect(temporary, temporary_entry, file.id, "inspect temporary file") ||
            !inspect(object, object_entry, file.id, "inspect object file"))
            return false;

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
                if (!remove_temporary(temporary, file.id)) return false;
            }
            return mark_ready(file.id, report);
        }

        if (temporary_entry.kind == EntryKind::Regular)
        {
            std::error_code link_error;
            std::filesystem::create_hard_link(temporary, object, link_error);
            if (link_error)
                return fail("link recovered object", file.id, link_error.message());
            if (!remove_temporary(temporary, file.id)) return false;
            return mark_ready(file.id, report);
        }

        const MetadataResult cancelled = store_.cancel_publishing(file.id);
        if (cancelled != MetadataResult::Ok)
            return fail("cancel missing publication", file.id,
                        cancelled == MetadataResult::NotFound
                            ? "publishing row disappeared"
                            : store_.error_message());
        ++report.cancelled;
        return true;
    }

    bool remove_temporary(const std::filesystem::path& temporary,
                          std::string_view id)
    {
        std::error_code error;
        const bool removed = std::filesystem::remove(temporary, error);
        if (error) return fail("remove recovered temporary file", id, error.message());
        if (!removed)
            return fail("remove recovered temporary file", id,
                        "temporary file disappeared");
        return true;
    }

    bool mark_ready(std::string_view id, PublishingRecoveryReport& report)
    {
        const MetadataResult ready = store_.mark_ready(id);
        if (ready != MetadataResult::Ok)
            return fail("mark recovered object ready", id,
                        ready == MetadataResult::NotFound
                            ? "publishing row disappeared"
                            : store_.error_message());
        ++report.completed;
        return true;
    }

    bool fail(std::string_view operation, std::string_view id,
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

    FileMetadataStore& store_;
    std::string error_message_;
};
// ===== DAY 26 ADDED END =====

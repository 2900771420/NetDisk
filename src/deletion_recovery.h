// ===== DAY 27 ADDED BEGIN 启动时幂等完成deleting对象清理 =====
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

struct DeletionRecoveryReport
{
    std::size_t completed = 0;
    std::size_t objects_removed = 0;
};

class DeletionRecovery
{
public:
    explicit DeletionRecovery(FileMetadataStore& store) noexcept
        : store_(store)
    {
    }

    bool run(DeletionRecoveryReport& report)
    {
        report = {};
        error_message_.clear();

        std::vector<FileMetadata> deleting;
        if (store_.list_deleting(deleting) != MetadataResult::Ok)
            return fail("list deleting metadata", "", store_.error_message());

        for (const FileMetadata& file : deleting)
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
    bool recover_one(const FileMetadata& file, DeletionRecoveryReport& report)
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
                            ? "deleting row disappeared"
                            : store_.error_message());

        ++report.completed;
        if (removed) ++report.objects_removed;
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
// ===== DAY 27 ADDED END =====

// ===== DAY 30 ADDED BEGIN 为旧ready对象幂等回填SHA256 =====
#pragma once

#include "file_metadata_store.h"
#include "object_path.h"
#include "sha256_hasher.h"
#include "unique_fd.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

enum class DigestBackfillFindingKind
{
    InvalidMetadataId,
    MissingObject,
    NonRegularObject,
    SizeMismatch,
    ChangedDuringRead
};

struct DigestBackfillFinding
{
    DigestBackfillFindingKind kind = DigestBackfillFindingKind::MissingObject;
    std::string id;
    std::uint64_t expected_size = 0;
    std::uint64_t actual_size = 0;
};

struct DigestBackfillReport
{
    std::size_t ready_records = 0;
    std::size_t already_hashed = 0;
    std::size_t backfilled = 0;
    std::size_t skipped = 0;
    std::vector<DigestBackfillFinding> findings;
};

inline const char* digest_backfill_kind_name(DigestBackfillFindingKind kind) noexcept
{
    switch (kind)
    {
        case DigestBackfillFindingKind::InvalidMetadataId:
            return "invalid_metadata_id";
        case DigestBackfillFindingKind::MissingObject:
            return "missing_object";
        case DigestBackfillFindingKind::NonRegularObject:
            return "non_regular_object";
        case DigestBackfillFindingKind::SizeMismatch:
            return "size_mismatch";
        case DigestBackfillFindingKind::ChangedDuringRead:
            return "changed_during_read";
    }
    return "unknown";
}

class DigestBackfiller
{
public:
    explicit DigestBackfiller(FileMetadataStore& store) noexcept
        : store_(store)
    {
    }

    bool run(DigestBackfillReport& report)
    {
        report = {};
        error_message_.clear();

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
                if (!file.sha256.empty())
                {
                    ++report.already_hashed;
                    continue;
                }
                if (!backfill_one(file, report)) return false;
            }

            if (!has_more) break;
            if (files.empty())
                return fail("list ready metadata", "pagination returned no cursor row");
            page.has_cursor = true;
            page.cursor_created_at = files.back().created_at;
            page.cursor_id = files.back().id;
        }
        return true;
    }

    const std::string& error_message() const noexcept
    {
        return error_message_;
    }

private:
    void skip(DigestBackfillReport& report, DigestBackfillFindingKind kind,
              const FileMetadata& file, std::uint64_t actual_size = 0)
    {
        ++report.skipped;
        report.findings.push_back(
            DigestBackfillFinding{kind, file.id, file.size, actual_size});
    }

    static bool stat_file(int fd, struct stat& output, int& error) noexcept
    {
        int result;
        do
        {
            result = ::fstat(fd, &output);
        } while (result == -1 && errno == EINTR);
        if (result == 0) return true;
        error = errno;
        return false;
    }

    bool backfill_one(const FileMetadata& file, DigestBackfillReport& report)
    {
        std::string path;
        if (!make_object_path(file.id, path))
        {
            skip(report, DigestBackfillFindingKind::InvalidMetadataId, file);
            return true;
        }

        int raw_fd;
        do
        {
            raw_fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        } while (raw_fd == -1 && errno == EINTR);
        if (raw_fd == -1)
        {
            const int error = errno;
            if (error == ENOENT || error == ENOTDIR)
            {
                skip(report, DigestBackfillFindingKind::MissingObject, file);
                return true;
            }
            if (error == ELOOP)
            {
                skip(report, DigestBackfillFindingKind::NonRegularObject, file);
                return true;
            }
            return fail_system("open object id=" + file.id, error);
        }
        UniqueFd object(raw_fd);

        struct stat before{};
        int stat_error = 0;
        if (!stat_file(object.get(), before, stat_error))
            return fail_system("inspect object id=" + file.id, stat_error);
        if (!S_ISREG(before.st_mode))
        {
            skip(report, DigestBackfillFindingKind::NonRegularObject, file);
            return true;
        }
        if (before.st_size < 0 || static_cast<std::uint64_t>(before.st_size) != file.size)
        {
            skip(report, DigestBackfillFindingKind::SizeMismatch, file,
                 before.st_size < 0 ? 0 : static_cast<std::uint64_t>(before.st_size));
            return true;
        }

        Sha256Hasher hasher;
        std::uint64_t remaining = file.size;
        char buffer[16 * 1024];
        while (remaining != 0)
        {
            const std::size_t count = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, sizeof(buffer)));
            const ssize_t received = ::read(object.get(), buffer, count);
            if (received == -1)
            {
                if (errno == EINTR) continue;
                return fail_system("read object id=" + file.id, errno);
            }
            if (received == 0)
            {
                skip(report, DigestBackfillFindingKind::ChangedDuringRead, file,
                     file.size - remaining);
                return true;
            }
            const auto bytes = static_cast<std::size_t>(received);
            if (!hasher.update(std::string_view(buffer, bytes)))
                return fail("hash object id=" + file.id, "OpenSSL EVP update failed");
            remaining -= bytes;
        }

        char extra = 0;
        ssize_t trailing;
        do
        {
            trailing = ::read(object.get(), &extra, 1);
        } while (trailing == -1 && errno == EINTR);
        if (trailing == -1)
            return fail_system("verify object end id=" + file.id, errno);
        if (trailing != 0)
        {
            skip(report, DigestBackfillFindingKind::ChangedDuringRead,
                 file, file.size + 1);
            return true;
        }

        struct stat after{};
        if (!stat_file(object.get(), after, stat_error))
            return fail_system("reinspect object id=" + file.id, stat_error);
        bool changed = before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
            before.st_size != after.st_size || before.st_mtime != after.st_mtime ||
            before.st_ctime != after.st_ctime;
#if defined(__linux__)
        changed = changed || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
            before.st_ctim.tv_nsec != after.st_ctim.tv_nsec;
#endif
        if (changed)
        {
            skip(report, DigestBackfillFindingKind::ChangedDuringRead, file,
                 after.st_size < 0 ? 0 : static_cast<std::uint64_t>(after.st_size));
            return true;
        }

        std::string digest;
        if (!hasher.finish(digest))
            return fail("hash object id=" + file.id, "OpenSSL EVP finalization failed");
        const MetadataResult stored =
            store_.set_sha256_if_missing(file.id, file.size, digest);
        if (stored == MetadataResult::Error)
            return fail("store digest id=" + file.id, store_.error_message());
        if (stored != MetadataResult::Ok)
            return fail("store digest id=" + file.id,
                        "ready metadata changed during backfill");

        ++report.backfilled;
        return true;
    }

    bool fail(std::string operation, std::string_view detail)
    {
        error_message_ = std::move(operation);
        error_message_ += ": ";
        error_message_.append(detail);
        return false;
    }

    bool fail_system(std::string operation, int error)
    {
        return fail(std::move(operation), std::strerror(error));
    }

    FileMetadataStore& store_;
    std::string error_message_;
};
// ===== DAY 30 ADDED END =====

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class FileMetadataStore;
struct FileMetadata;

// ===== DAY 33A ADDED BEGIN 提取摘要回填稳定接口 =====
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

const char* digest_backfill_kind_name(DigestBackfillFindingKind kind) noexcept;

class DigestBackfiller
{
public:
    explicit DigestBackfiller(FileMetadataStore& store) noexcept;
    bool run(DigestBackfillReport& report);
    const std::string& error_message() const noexcept;

private:
    void skip(DigestBackfillReport& report, DigestBackfillFindingKind kind,
              const FileMetadata& file, std::uint64_t actual_size = 0);
    bool backfill_one(const FileMetadata& file, DigestBackfillReport& report);
    bool fail(std::string operation, std::string_view detail);
    bool fail_system(std::string operation, int error);

    FileMetadataStore& store_;
    std::string error_message_;
};
// ===== DAY 33A ADDED END =====

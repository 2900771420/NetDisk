#pragma once

#include <cstddef>
#include <string>
#include <string_view>

class FileMetadataStore;
struct FileMetadata;

// ===== DAY 33A ADDED BEGIN 合并发布与删除恢复稳定接口 =====
struct PublishingRecoveryReport
{
    std::size_t completed = 0;
    std::size_t cancelled = 0;
};

class PublishingRecovery
{
public:
    explicit PublishingRecovery(FileMetadataStore& store) noexcept;
    bool run(PublishingRecoveryReport& report);
    const std::string& error_message() const noexcept;

private:
    bool recover_one(const FileMetadata& file, PublishingRecoveryReport& report);
    bool mark_ready(std::string_view id, PublishingRecoveryReport& report);
    bool fail(std::string_view operation, std::string_view id,
              std::string_view detail);

    FileMetadataStore& store_;
    std::string error_message_;
};

struct DeletionRecoveryReport
{
    std::size_t completed = 0;
    std::size_t objects_removed = 0;
};

class DeletionRecovery
{
public:
    explicit DeletionRecovery(FileMetadataStore& store) noexcept;
    bool run(DeletionRecoveryReport& report);
    const std::string& error_message() const noexcept;

private:
    bool recover_one(const FileMetadata& file, DeletionRecoveryReport& report);
    bool fail(std::string_view operation, std::string_view id,
              std::string_view detail);

    FileMetadataStore& store_;
    std::string error_message_;
};
// ===== DAY 33A ADDED END =====

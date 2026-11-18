// ===== DAY 33A ADDED BEGIN 合并SHA256格式规则与增量摘要接口 =====
#pragma once

#include <string>
#include <string_view>

bool is_canonical_sha256(std::string_view value) noexcept;

class Sha256Hasher
{
public:
    Sha256Hasher() noexcept = default;
    Sha256Hasher(const Sha256Hasher&) = delete;
    Sha256Hasher& operator=(const Sha256Hasher&) = delete;
    ~Sha256Hasher() noexcept;

    bool update(std::string_view data) noexcept;
    bool finish(std::string& output) noexcept;
    bool good() const noexcept;

private:
    bool initialize() noexcept;
    void fail() noexcept;

    void* context_ = nullptr;
    bool finalized_ = false;
    bool failed_ = false;
};
// ===== DAY 33A ADDED END =====

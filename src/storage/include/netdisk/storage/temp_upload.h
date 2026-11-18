#pragma once

#include <netdisk/crypto/sha256.h>

#include <cstddef>
#include <string>
#include <string_view>

// ===== DAY 33A ADDED BEGIN 提取临时上传生命周期稳定接口 =====
class TempUpload
{
public:
    TempUpload() = default;
    TempUpload(const TempUpload&) = delete;
    TempUpload& operator=(const TempUpload&) = delete;
    ~TempUpload() noexcept;

    bool append(std::string_view data);
    bool close_file();
    int error() const noexcept;
    const char* path() const noexcept;
    bool prepare_publish(std::size_t expected);
    bool publish_prepared();
    bool publish(std::size_t expected);
    bool discard_published();
    const std::string& id() const noexcept;
    const std::string& sha256() const noexcept;

private:
    bool fail(int error) noexcept;

    char path_[sizeof("data/uploads/upload-XXXXXX")] = "data/uploads/upload-XXXXXX";
    int fd_ = -1;
    int error_ = 0;
    bool created_ = false;
    std::size_t written_ = 0;
    std::string id_;
    bool prepared_ = false;
    bool published_ = false;
    Sha256Hasher sha256_;
    std::string sha256_hex_;
};
// ===== DAY 33A ADDED END =====

#pragma once

#include "object_path.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <unistd.h>
#include <cstring>
#include <string>
#include <utility>

class TempUpload
{
public:
    TempUpload() = default;
    TempUpload(const TempUpload&) = delete;
    TempUpload& operator=(const TempUpload&) = delete;

    ~TempUpload() noexcept
    {
        const int saved = errno;
        if (fd_ >= 0 && ::close(fd_) == -1) std::perror("close temporary file");
        if (created_ && ::unlink(path_) == -1 && errno != ENOENT)
            std::perror("unlink temporary file");
        errno = saved;
    }

    bool append(std::string_view data)
    {
        if (error_ != 0) return false;
        if (!id_.empty()) return fail(EINVAL);
        if (!created_)
        {
            fd_ = ::mkstemp(path_);
            if (fd_ == -1) return fail(errno);
            created_ = true;
        }
        if (fd_ < 0) return fail(EBADF);
        std::size_t written = 0;
        while (written < data.size())
        {
            const ssize_t n = ::write(fd_, data.data() + written, data.size() - written);
            if (n == -1)
            {
                if (errno == EINTR) continue;
                return fail(errno);
            }
            if (n == 0) return fail(EIO);
            written += static_cast<std::size_t>(n);
            written_ += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool close_file()
    {
        if (fd_ >= 0)
        {
            const int raw = fd_;
            fd_ = -1;
            if (::close(raw) == -1) return fail(errno);
        }
        return error_ == 0;
    }

    int error() const noexcept { return error_; }
    const char* path() const noexcept { return created_ ? path_ : ""; }

    // ===== DAY 25 ADDED BEGIN 在文件发布前冻结长度并取得稳定ID =====
    bool prepare_publish(std::size_t expected)
    {
        if (error_ != 0) return false;
        if (!created_ || prepared_ || published_ || !id_.empty() || written_ != expected)
            return fail(EINVAL);
        if (!close_file()) return false;
        const char* separator = std::strrchr(path_, '/');
        if (separator == nullptr || separator[1] == '\0') return fail(EINVAL);
        std::string candidate(separator + 1);
        std::string destination;
        if (!make_object_path(candidate, destination)) return fail(EINVAL);
        id_ = std::move(candidate);
        prepared_ = true;
        return true;
    }

    bool publish_prepared()
    {
        if (error_ != 0) return false;
        if (!created_ || !prepared_ || published_ || id_.empty() || fd_ >= 0)
            return fail(EINVAL);
        std::string destination;
        if (!make_object_path(id_, destination)) return fail(EINVAL);
        if (::link(path_, destination.c_str()) == -1) return fail(errno);
        published_ = true;
        if (::unlink(path_) == 0)
            created_ = false;
        else
            std::perror("unlink published temporary name");
        return true;
    }

    bool publish(std::size_t expected)
    {
        return prepare_publish(expected) && publish_prepared();
    }
    // ===== DAY 25 ADDED END =====

    // ===== DAY 25 MODIFIED BEGIN 只允许删除已经链接的正式对象 =====
    bool discard_published()
    {
        if (id_.empty() || !published_) return fail(EINVAL);
        std::string destination;
        if (!make_object_path(id_, destination)) return fail(EINVAL);
        if (::unlink(destination.c_str()) == -1) return fail(errno);
        published_ = false;
        id_.clear();
        return true;
    }
    // ===== DAY 25 MODIFIED END =====

    const std::string& id() const noexcept { return id_; }

private:
    bool fail(int error) noexcept
    {
        error_ = error;
        return false;
    }

    char path_[sizeof("data/uploads/upload-XXXXXX")] = "data/uploads/upload-XXXXXX";
    int fd_ = -1;
    int error_ = 0;
    bool created_ = false;
    std::size_t written_ = 0;
    std::string id_;
    // ===== DAY 25 ADDED BEGIN 区分已准备与已链接状态 =====
    bool prepared_ = false;
    bool published_ = false;
    // ===== DAY 25 ADDED END =====
};

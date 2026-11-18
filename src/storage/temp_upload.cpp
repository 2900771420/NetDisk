#include <netdisk/storage/temp_upload.h>

#include <netdisk/storage/object_path.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <utility>

// ===== DAY 33A ADDED BEGIN 将临时上传实现移出头文件 =====
TempUpload::~TempUpload() noexcept
{
    const int saved = errno;
    if (fd_ >= 0 && ::close(fd_) == -1) std::perror("close temporary file");
    if (created_ && ::unlink(path_) == -1 && errno != ENOENT)
        std::perror("unlink temporary file");
    errno = saved;
}

bool TempUpload::append(std::string_view data)
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
        const auto count = static_cast<std::size_t>(n);
        if (!sha256_.update(data.substr(written, count))) return fail(EIO);
        written += count;
        written_ += count;
    }
    return true;
}

bool TempUpload::close_file()
{
    if (fd_ >= 0)
    {
        const int raw = fd_;
        fd_ = -1;
        if (::close(raw) == -1) return fail(errno);
    }
    return error_ == 0;
}

int TempUpload::error() const noexcept
{
    return error_;
}

const char* TempUpload::path() const noexcept
{
    return created_ ? path_ : "";
}

bool TempUpload::prepare_publish(std::size_t expected)
{
    if (error_ != 0) return false;
    if (!created_ || prepared_ || published_ || !id_.empty() || written_ != expected)
        return fail(EINVAL);
    if (!close_file()) return false;
    if (!sha256_.finish(sha256_hex_)) return fail(EIO);
    const char* separator = std::strrchr(path_, '/');
    if (separator == nullptr || separator[1] == '\0') return fail(EINVAL);
    std::string candidate(separator + 1);
    std::string destination;
    if (!make_object_path(candidate, destination)) return fail(EINVAL);
    id_ = std::move(candidate);
    prepared_ = true;
    return true;
}

bool TempUpload::publish_prepared()
{
    if (error_ != 0) return false;
    if (!created_ || !prepared_ || published_ || id_.empty() || fd_ >= 0)
        return fail(EINVAL);
    std::string destination;
    if (!make_object_path(id_, destination)) return fail(EINVAL);
    if (::link(path_, destination.c_str()) == -1) return fail(errno);
    published_ = true;
    if (::unlink(path_) == 0) created_ = false;
    else std::perror("unlink published temporary name");
    return true;
}

bool TempUpload::publish(std::size_t expected)
{
    return prepare_publish(expected) && publish_prepared();
}

bool TempUpload::discard_published()
{
    if (id_.empty() || !published_) return fail(EINVAL);
    std::string destination;
    if (!make_object_path(id_, destination)) return fail(EINVAL);
    if (::unlink(destination.c_str()) == -1) return fail(errno);
    published_ = false;
    id_.clear();
    return true;
}

const std::string& TempUpload::id() const noexcept
{
    return id_;
}

const std::string& TempUpload::sha256() const noexcept
{
    return sha256_hex_;
}

bool TempUpload::fail(int error) noexcept
{
    error_ = error;
    return false;
}
// ===== DAY 33A ADDED END =====

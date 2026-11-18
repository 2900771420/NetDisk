#include <netdisk/client/download_session.h>

#include <netdisk/api/file_id.h>
#include <netdisk/client/download_protocol.h>
#include <netdisk/crypto/sha256.h>
#include <netdisk/http/response.h>
#include <netdisk/io/posix_io.h>
#include <netdisk/io/unique_fd.h>

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
// ===== DAY 35 ADDED BEGIN 使用rename发布已校验临时文件 =====
#include <cstdio>
// ===== DAY 35 ADDED END =====
#include <poll.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
constexpr std::size_t max_response_head_size = 8192;
constexpr int max_timeout_ms = 600000;
using MonotonicClock = std::chrono::steady_clock;

// ===== DAY 35 ADDED BEGIN 区分续传来源是临时产物还是已发布文件 =====
enum class ResumeSource
{
    None,
    Partial,
    Published
};
// ===== DAY 35 ADDED END =====

// ===== DAY 35 MODIFIED BEGIN 同时检查最终路径与临时产物路径 =====
bool inspect_regular_file(const std::string& path, bool& exists,
                          std::uint64_t& size)
{
    struct stat info{};
    if (::lstat(path.c_str(), &info) == -1)
    {
        if (errno == ENOENT)
        {
            exists = false;
            size = 0;
            return true;
        }
        print_system_error("lstat download path", errno);
        return false;
    }
    if (!S_ISREG(info.st_mode) || info.st_size < 0)
    {
        std::cerr << "download path must be a regular file: "
                  << path << '\n';
        return false;
    }
    exists = true;
    size = static_cast<std::uint64_t>(info.st_size);
    return true;
}
// ===== DAY 35 MODIFIED END =====

bool read_etag_state(const std::string& path, std::string& etag)
{
    int raw_fd;
    do
    {
        raw_fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    } while (raw_fd == -1 && errno == EINTR);
    if (raw_fd == -1) return false;
    UniqueFd file(raw_fd);

    struct stat info{};
    if (::fstat(file.get(), &info) == -1 || !S_ISREG(info.st_mode) ||
        info.st_size < 0 || info.st_size > 128)
        return false;
    std::string content(static_cast<std::size_t>(info.st_size), '\0');
    std::size_t received = 0;
    while (received < content.size())
    {
        const ssize_t n = ::read(file.get(), content.data() + received,
                                 content.size() - received);
        if (n == -1)
        {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        received += static_cast<std::size_t>(n);
    }
    if (!content.empty() && content.back() == '\n') content.pop_back();
    if (!content.empty() && content.back() == '\r') content.pop_back();
    if (!download_client::is_strong_sha256_etag(content)) return false;
    etag = std::move(content);
    return true;
}

bool write_all_file(int fd, const char* data, std::size_t size)
{
    std::size_t written = 0;
    while (written < size)
    {
        const ssize_t n = ::write(fd, data + written, size - written);
        if (n == -1)
        {
            if (errno == EINTR) continue;
            print_system_error("write download", errno);
            return false;
        }
        if (n == 0)
        {
            std::cerr << "write download made no progress\n";
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    return true;
}

bool save_etag_state(const std::string& path, std::string_view etag)
{
    int raw_fd;
    do
    {
        raw_fd = ::open(path.c_str(),
            O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK,
            0600);
    } while (raw_fd == -1 && errno == EINTR);
    if (raw_fd == -1)
    {
        print_system_error("open ETag state", errno);
        return false;
    }
    UniqueFd file(raw_fd);
    struct stat info{};
    if (::fstat(file.get(), &info) == -1 || !S_ISREG(info.st_mode))
    {
        std::cerr << "ETag state path must be a regular file\n";
        return false;
    }
    const std::string content = std::string(etag) + "\n";
    return write_all_file(file.get(), content.data(), content.size());
}

void report_timeout(const char* operation, int timeout_ms)
{
    std::cerr << operation << " timed out after " << timeout_ms << " ms\n";
}

bool wait_for_socket(int socket_fd, short events,
                     MonotonicClock::time_point deadline,
                     const char* operation, int timeout_ms)
{
    while (true)
    {
        const auto now = MonotonicClock::now();
        if (now >= deadline)
        {
            report_timeout(operation, timeout_ms);
            return false;
        }
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now).count();
        if (remaining <= 0) remaining = 1;

        pollfd descriptor{};
        descriptor.fd = socket_fd;
        descriptor.events = events;
        const int result = ::poll(&descriptor, 1, static_cast<int>(remaining));
        if (result == -1)
        {
            if (errno == EINTR) continue;
            print_system_error(operation, errno);
            return false;
        }
        if (result == 0)
        {
            report_timeout(operation, timeout_ms);
            return false;
        }
        if ((descriptor.revents & POLLNVAL) != 0)
        {
            std::cerr << operation << " failed because the socket is invalid\n";
            return false;
        }
        if ((descriptor.revents & (events | POLLERR | POLLHUP)) != 0)
            return true;
    }
}

bool connect_to_server(const download_client::DownloadOptions& options,
                       int& socket_fd)
{
    const int raw_fd = ::socket(
        AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (raw_fd == -1)
    {
        print_system_error("socket", errno);
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.server_port);
    if (::inet_pton(AF_INET, options.server_ipv4.c_str(),
                    &address.sin_addr) != 1)
    {
        std::cerr << "server host must be a numeric IPv4 address\n";
        ::close(raw_fd);
        return false;
    }

    const int result = ::connect(
        raw_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (result == 0)
    {
        socket_fd = raw_fd;
        return true;
    }
    if (errno != EINPROGRESS && errno != EALREADY)
    {
        const int error = errno;
        ::close(raw_fd);
        print_system_error("connect", error);
        return false;
    }

    const auto deadline = MonotonicClock::now() +
        std::chrono::milliseconds(options.timeout_ms);
    while (wait_for_socket(raw_fd, POLLOUT, deadline,
                           "connect", options.timeout_ms))
    {
        int socket_error = 0;
        socklen_t error_size = sizeof(socket_error);
        if (::getsockopt(raw_fd, SOL_SOCKET, SO_ERROR,
                         &socket_error, &error_size) == -1)
        {
            const int error = errno;
            ::close(raw_fd);
            print_system_error("getsockopt SO_ERROR", error);
            return false;
        }
        if (socket_error == 0)
        {
            socket_fd = raw_fd;
            return true;
        }
        if (socket_error == EINPROGRESS || socket_error == EALREADY)
            continue;
        ::close(raw_fd);
        print_system_error("connect", socket_error);
        return false;
    }
    ::close(raw_fd);
    return false;
}

bool send_with_timeout(int socket_fd, const char* data, std::size_t size,
                       int timeout_ms)
{
    std::size_t sent = 0;
    auto deadline = MonotonicClock::now() +
        std::chrono::milliseconds(timeout_ms);
    while (sent < size)
    {
        if (MonotonicClock::now() >= deadline)
        {
            report_timeout("send request", timeout_ms);
            return false;
        }
        const ssize_t count = ::send(
            socket_fd, data + sent, size - sent,
            MSG_NOSIGNAL | MSG_DONTWAIT);
        if (count > 0)
        {
            sent += static_cast<std::size_t>(count);
            deadline = MonotonicClock::now() +
                std::chrono::milliseconds(timeout_ms);
            continue;
        }
        if (count == 0)
        {
            std::cerr << "send request made no progress\n";
            return false;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            if (!wait_for_socket(socket_fd, POLLOUT, deadline,
                                 "send request", timeout_ms))
                return false;
            continue;
        }
        print_system_error("send request", errno);
        return false;
    }
    return true;
}

ssize_t receive_with_timeout(int socket_fd, char* data, std::size_t size,
                             int timeout_ms, const char* operation)
{
    const auto deadline = MonotonicClock::now() +
        std::chrono::milliseconds(timeout_ms);
    while (true)
    {
        if (MonotonicClock::now() >= deadline)
        {
            report_timeout(operation, timeout_ms);
            return -1;
        }
        const ssize_t count = ::recv(
            socket_fd, data, size, MSG_DONTWAIT);
        if (count >= 0) return count;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            if (!wait_for_socket(socket_fd, POLLIN, deadline,
                                 operation, timeout_ms))
                return -1;
            continue;
        }
        print_system_error(operation, errno);
        return -1;
    }
}
bool receive_response_head(int socket_fd, std::string& head,
                           std::string& body_prefix, int timeout_ms)
{
    std::string received;
    char buffer[4096];
    while (true)
    {
        const auto boundary = received.find("\r\n\r\n");
        if (boundary != std::string::npos)
        {
            const auto head_size = boundary + 4;
            if (head_size > max_response_head_size)
            {
                std::cerr << "response head exceeds limit\n";
                return false;
            }
            head.assign(received.data(), head_size);
            body_prefix.assign(received.data() + head_size,
                               received.size() - head_size);
            return true;
        }
        if (received.size() > max_response_head_size)
        {
            std::cerr << "response head exceeds limit\n";
            return false;
        }
        const ssize_t n = receive_with_timeout(
            socket_fd, buffer, sizeof(buffer), timeout_ms,
            "receive response head");
        if (n == -1) return false;
        if (n == 0)
        {
            std::cerr << "server closed before response head completed\n";
            return false;
        }
        received.append(buffer, static_cast<std::size_t>(n));
    }
}

// ===== DAY 35 ADDED BEGIN 复制旧最终文件前缀到临时产物再继续追加 =====
bool copy_published_prefix(const std::string& published_path,
                           const std::string& partial_path,
                           std::uint64_t expected_size,
                           int& partial_fd)
{
    int raw_source;
    do
    {
        raw_source = ::open(published_path.c_str(),
            O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    } while (raw_source == -1 && errno == EINTR);
    if (raw_source == -1)
    {
        print_system_error("open published download prefix", errno);
        return false;
    }
    UniqueFd source(raw_source);

    struct stat source_info{};
    if (::fstat(source.get(), &source_info) == -1 ||
        !S_ISREG(source_info.st_mode) || source_info.st_size < 0 ||
        static_cast<std::uint64_t>(source_info.st_size) != expected_size)
    {
        std::cerr << "published download changed before prefix copy\n";
        return false;
    }

    int raw_partial;
    do
    {
        raw_partial = ::open(partial_path.c_str(),
            O_WRONLY | O_CREAT | O_TRUNC | O_APPEND |
            O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK,
            0600);
    } while (raw_partial == -1 && errno == EINTR);
    if (raw_partial == -1)
    {
        print_system_error("open partial download for prefix copy", errno);
        return false;
    }

    struct stat partial_info{};
    if (::fstat(raw_partial, &partial_info) == -1 ||
        !S_ISREG(partial_info.st_mode))
    {
        std::cerr << "partial download path must be a regular file\n";
        ::close(raw_partial);
        return false;
    }

    std::uint64_t remaining = expected_size;
    char buffer[16 * 1024];
    while (remaining != 0)
    {
        const auto count = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, sizeof(buffer)));
        const ssize_t n = ::read(source.get(), buffer, count);
        if (n == -1)
        {
            if (errno == EINTR) continue;
            print_system_error("read published download prefix", errno);
            ::close(raw_partial);
            return false;
        }
        if (n == 0)
        {
            std::cerr << "published download ended during prefix copy\n";
            ::close(raw_partial);
            return false;
        }
        const auto size = static_cast<std::size_t>(n);
        if (!write_all_file(raw_partial, buffer, size))
        {
            ::close(raw_partial);
            return false;
        }
        remaining -= size;
    }

    if (::fstat(raw_partial, &partial_info) == -1 ||
        partial_info.st_size < 0 ||
        static_cast<std::uint64_t>(partial_info.st_size) != expected_size)
    {
        std::cerr << "partial download size differs after prefix copy\n";
        ::close(raw_partial);
        return false;
    }
    partial_fd = raw_partial;
    return true;
}
// ===== DAY 35 ADDED END =====

// ===== DAY 35 DELETED 删除直接写入最终下载路径的输出打开逻辑 =====
// ===== DAY 35 MODIFIED BEGIN 所有新正文只写入同目录临时产物 =====
bool open_partial_for_plan(const std::string& partial_path,
                           const std::string& published_path,
                           download_client::ResumeAction action,
                           ResumeSource source,
                           std::uint64_t expected_size,
                           int& partial_fd)
{
    if (action == download_client::ResumeAction::Append &&
        source == ResumeSource::Published)
    {
        return copy_published_prefix(
            published_path, partial_path, expected_size, partial_fd);
    }

    int flags = O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
    if (action == download_client::ResumeAction::Replace)
        flags |= O_CREAT | O_TRUNC;
    else
        flags |= O_APPEND;

    int raw_fd;
    do
    {
        raw_fd = ::open(partial_path.c_str(), flags, 0600);
    } while (raw_fd == -1 && errno == EINTR);
    if (raw_fd == -1)
    {
        print_system_error("open partial download", errno);
        return false;
    }

    struct stat info{};
    if (::fstat(raw_fd, &info) == -1 || !S_ISREG(info.st_mode) ||
        info.st_size < 0 ||
        (action == download_client::ResumeAction::Append &&
         (source != ResumeSource::Partial ||
          static_cast<std::uint64_t>(info.st_size) != expected_size)))
    {
        std::cerr << "partial download changed before response body write\n";
        ::close(raw_fd);
        return false;
    }
    partial_fd = raw_fd;
    return true;
}
// ===== DAY 35 MODIFIED END =====

bool receive_body(int socket_fd, int output_fd, std::string_view prefix,
                  std::uint64_t content_length, int timeout_ms)
{
    if (prefix.size() > content_length)
    {
        std::cerr << "response body exceeds Content-Length\n";
        return false;
    }
    if (!write_all_file(output_fd, prefix.data(), prefix.size())) return false;
    std::uint64_t remaining = content_length - prefix.size();
    char buffer[16 * 1024];
    while (remaining != 0)
    {
        const auto count = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, sizeof(buffer)));
        const ssize_t n = receive_with_timeout(
            socket_fd, buffer, count, timeout_ms,
            "receive response body");
        if (n == -1) return false;
        if (n == 0)
        {
            std::cerr << "server closed before Content-Length bytes arrived\n";
            return false;
        }
        const auto size = static_cast<std::size_t>(n);
        if (!write_all_file(output_fd, buffer, size)) return false;
        remaining -= size;
    }
    return true;
}

bool hash_complete_file(const std::string& path, std::uint64_t expected_size,
                        std::string& digest)
{
    int raw_fd;
    do
    {
        raw_fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    } while (raw_fd == -1 && errno == EINTR);
    if (raw_fd == -1)
    {
        print_system_error("open completed download", errno);
        return false;
    }
    UniqueFd file(raw_fd);
    struct stat info{};
    if (::fstat(file.get(), &info) == -1 || !S_ISREG(info.st_mode) ||
        info.st_size < 0 || static_cast<std::uint64_t>(info.st_size) != expected_size)
    {
        std::cerr << "completed download size differs from response metadata\n";
        return false;
    }
    Sha256Hasher hasher;
    char buffer[16 * 1024];
    while (true)
    {
        const ssize_t n = ::read(file.get(), buffer, sizeof(buffer));
        if (n == -1)
        {
            if (errno == EINTR) continue;
            print_system_error("read completed download", errno);
            return false;
        }
        if (n == 0) break;
        if (!hasher.update(std::string_view(buffer, static_cast<std::size_t>(n))))
        {
            std::cerr << "SHA256 update failed\n";
            return false;
        }
    }
    if (!hasher.finish(digest))
    {
        std::cerr << "SHA256 finish failed\n";
        return false;
    }
    return true;
}

// ===== DAY 35 ADDED BEGIN 摘要通过后原子替换最终下载路径 =====
bool publish_partial_download(const std::string& partial_path,
                              const std::string& output_path)
{
    if (std::rename(partial_path.c_str(), output_path.c_str()) == -1)
    {
        print_system_error("rename verified partial download", errno);
        return false;
    }
    return true;
}
// ===== DAY 35 ADDED END =====

void remove_invalid_state(const std::string& path)
{
    if (::unlink(path.c_str()) == -1 && errno != ENOENT)
        print_system_error("remove invalid ETag state", errno);
}
}

int run_download(std::string_view id, const std::string& output_path)
{
    return run_download(id, output_path, download_client::DownloadOptions{});
}

// ===== DAY 35 MODIFIED BEGIN 使用临时产物恢复并在完整校验后原子发布 =====
int run_download(std::string_view id, const std::string& output_path,
                 const download_client::DownloadOptions& options)
{
    const std::string state_path = output_path + ".netdisk-etag";
    const std::string partial_path = output_path + ".netdisk-part";
    in_addr parsed_address{};
    if (!is_valid_file_id(id) || output_path.empty() ||
        options.server_port == 0 || options.timeout_ms <= 0 ||
        options.timeout_ms > max_timeout_ms ||
        ::inet_pton(AF_INET, options.server_ipv4.c_str(),
                    &parsed_address) != 1)
    {
        std::cerr << "invalid file ID output path or download options\n";
        return 2;
    }

    bool output_exists = false;
    std::uint64_t output_size = 0;
    bool partial_exists = false;
    std::uint64_t partial_size = 0;
    if (!inspect_regular_file(output_path, output_exists, output_size) ||
        !inspect_regular_file(partial_path, partial_exists, partial_size))
        return 1;

    ResumeSource source = ResumeSource::None;
    std::uint64_t local_size = 0;
    std::string saved_etag;
    if (partial_exists)
    {
        if (partial_size != 0 &&
            read_etag_state(state_path, saved_etag))
        {
            source = ResumeSource::Partial;
            local_size = partial_size;
        }
    }
    else if (output_exists && output_size != 0 &&
             read_etag_state(state_path, saved_etag))
    {
        source = ResumeSource::Published;
        local_size = output_size;
    }
    const bool resume = source != ResumeSource::None;

    const std::string authority = options.server_ipv4 + ":" +
                                  std::to_string(options.server_port);
    std::string request;
    if (!download_client::make_download_request(
            id, resume, local_size, saved_etag, authority, request))
    {
        std::cerr << "failed to build download request\n";
        return 1;
    }

    int raw_socket = -1;
    if (!connect_to_server(options, raw_socket)) return 1;
    UniqueFd socket(raw_socket);
    if (!send_with_timeout(socket.get(), request.data(), request.size(),
                           options.timeout_ms))
        return 1;

    std::string response_wire;
    std::string body_prefix;
    if (!receive_response_head(socket.get(), response_wire, body_prefix,
                               options.timeout_ms))
        return 1;
    download_client::ResponseHead response;
    if (!download_client::parse_response_head(response_wire, response))
    {
        std::cerr << "invalid HTTP response head\n";
        return 1;
    }
    const auto plan = download_client::make_resume_plan(
        response, local_size, saved_etag);
    if (plan.action == download_client::ResumeAction::Reject)
    {
        std::cerr << "server response does not satisfy download contract status="
                  << response.status << '\n';
        return 1;
    }
    if ((plan.action == download_client::ResumeAction::Replace ||
         plan.action == download_client::ResumeAction::Append) &&
        body_prefix.size() > response.content_length)
    {
        std::cerr << "response body exceeds Content-Length\n";
        return 1;
    }

    if (plan.action == download_client::ResumeAction::Replace ||
        plan.action == download_client::ResumeAction::Append)
    {
        int raw_partial = -1;
        if (!open_partial_for_plan(
                partial_path, output_path, plan.action,
                source, local_size, raw_partial))
            return 1;
        UniqueFd partial(raw_partial);
        if (plan.action == download_client::ResumeAction::Replace &&
            !save_etag_state(state_path, response.etag))
            return 1;
        if (!receive_body(socket.get(), partial.get(), body_prefix,
                          response.content_length, options.timeout_ms))
            return 1;
    }

    const bool already_published =
        plan.action == download_client::ResumeAction::Complete &&
        source == ResumeSource::Published;
    const std::string& completed_path =
        already_published ? output_path : partial_path;

    std::string digest;
    if (!hash_complete_file(completed_path, plan.total_size, digest) ||
        response.etag.substr(1, 64) != digest)
    {
        std::cerr << "completed download SHA256 differs from ETag\n";
        remove_invalid_state(state_path);
        return 1;
    }
    if (!already_published &&
        !publish_partial_download(partial_path, output_path))
        return 1;

    const char* result = plan.action == download_client::ResumeAction::Append
        ? "resumed with 206"
        : plan.action == download_client::ResumeAction::Replace
            ? "replaced with 200" : "already complete after 416";
    std::cout << "download complete: " << result
              << " bytes=" << plan.total_size << " sha256=" << digest << '\n';
    return 0;
}
// ===== DAY 35 MODIFIED END =====

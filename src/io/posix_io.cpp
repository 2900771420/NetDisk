// ===== DAY 33A ADDED BEGIN 实现POSIX错误输出与短写重试 =====
#include <netdisk/io/posix_io.h>

#include <cerrno>
#include <cstring>
#include <iostream>

void print_system_error(const char* operation, int error)
{
    std::cerr << operation << " failed: "
              << std::strerror(error)
              << " (errno=" << error << ")\n";
}

bool send_all(int fd, const char* data, std::size_t size,
              SendFunction send_function)
{
    std::size_t sent = 0;
    while (sent < size)
    {
        const ssize_t count = send_function(
            fd, data + sent, size - sent, MSG_NOSIGNAL);
        if (count == -1)
        {
            const int error = errno;
            if (error == EINTR) continue;
            print_system_error("send", error);
            return false;
        }
        if (count == 0)
        {
            std::cerr << "send made no progress\n";
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}
// ===== DAY 33A ADDED END =====

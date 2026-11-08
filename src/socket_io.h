#pragma once

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <sys/socket.h>

inline void print_system_error(const char* operation, int error)
{
    std::cerr << operation << " failed: "
              << std::strerror(error)
              << " (errno=" << error << ")\n";
}

using SendFunction = ssize_t (*)(int, const void*, std::size_t, int);

inline bool send_all(
    int fd, const char* data, std::size_t size,
    SendFunction send_function = ::send)
{
    std::size_t sent = 0;
    while (sent < size)
    {
        const ssize_t n = send_function(
            fd, data + sent, size - sent, MSG_NOSIGNAL);
        if (n == -1)
        {
            const int error = errno;
            if (error == EINTR)
            {
                continue;
            }
            print_system_error("send", error);
            return false;
        }
        if (n == 0)
        {
            std::cerr << "send made no progress\n";
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

// ===== DAY 33A ADDED BEGIN 声明POSIX错误输出与完整Socket发送 =====
#pragma once

#include <cstddef>
#include <sys/socket.h>

using SendFunction = ssize_t (*)(int, const void*, std::size_t, int);

void print_system_error(const char* operation, int error);

bool send_all(int fd, const char* data, std::size_t size,
              SendFunction send_function = ::send);
// ===== DAY 33A ADDED END =====

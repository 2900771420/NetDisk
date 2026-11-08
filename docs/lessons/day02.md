# 第 02 单元 文件描述符所有权 RAII 与错误输出

教学必须在对话完整发送 此文件供助手保存上下文
用户认为第一天过于简单 明确要求继续第二天 不据此推断第一天 Linux 验收通过

## 目标和改动
从打印信息改为打开指定文件 通过 UniqueFd 管理生命周期 正常返回和模拟业务失败均自动关闭
新增 src/unique_fd.h 替换 src/main.cpp CMakeLists.txt 不变
今天只打开和关闭 不读取内容 不实现网络

## 完整源码

### src/unique_fd.h
```cpp
#pragma once

#include <cerrno>
#include <unistd.h>

class UniqueFd
{
public:
    explicit UniqueFd(int fd) noexcept : fd_(fd)
    {
    }

    ~UniqueFd() noexcept
    {
        if (fd_ >= 0)
        {
            const int saved_errno = errno;
            ::close(fd_);
            errno = saved_errno;
        }
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&&) = delete;
    UniqueFd& operator=(UniqueFd&&) = delete;

    int get() const noexcept
    {
        return fd_;
    }

private:
    int fd_;
};
```

### src/main.cpp
```cpp
#include "unique_fd.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string_view>

int inspect_file(const char* path, bool fail_after_open)
{
    const int raw_fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (raw_fd == -1)
    {
        const int error = errno;
        std::cerr << "open failed: " << path
                  << ": " << std::strerror(error)
                  << " (errno=" << error << ")\n";
        return 1;
    }

    UniqueFd file(raw_fd);
    std::cout << "opened fd=" << file.get() << '\n';

    if (fail_after_open)
    {
        std::cerr << "simulated failure after open\n";
        return 2;
    }

    std::cout << "normal return\n";
    return 0;
}

int main(int argc, char* argv[])
{
    if (argc != 2 && argc != 3)
    {
        std::cerr << "usage: " << argv[0]
                  << " <path> [--fail-after-open]\n";
        return 2;
    }

    const bool fail_after_open = argc == 3;
    if (fail_after_open &&
        std::string_view(argv[2]) != "--fail-after-open")
    {
        std::cerr << "unknown option: " << argv[2] << '\n';
        return 2;
    }

    return inspect_file(argv[1], fail_after_open);
}
```

### CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.16)

project(netdisk LANGUAGES CXX)

add_executable(netdisk src/main.cpp)

set_target_properties(netdisk PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
```

## 接口和生命周期
open 在 fcntl.h 声明 成功返回非负 int 失败返回 -1 并设置 errno
O_RDONLY 只读 O_CLOEXEC 在成功执行 exec 时关闭 此处没有 O_CREAT 不传创建权限参数
fd 是进程内描述符表的索引 并非文件内容或地址 0 也是合法 fd
UniqueFd 构造不打开文件 只接管 fd 调用者必须传入独占拥有的描述符
get 只借用整数 不转移所有权 不得 close(file.get()) 或创建第二个 owner
析构在正常离开作用域或异常栈展开时执行 不保证 SIGKILL 或 _exit 执行析构
删除复制构造和赋值 防止两个对象重复关闭 同时显式禁用移动 本单元不引入所有权转移
noexcept 表示不向外传播异常 const 表示 get 不修改对象
析构保存和恢复 errno 不覆盖原先错误 忽略 close 返回值是当前清理策略 不证明持久化成功
Linux close 失败时也不盲目重试 包括 EINTR 因 fd 可能已释放复用 此策略不外推所有操作系统
strerror 把保存的错误号转换为说明 当前为单线程诊断
argc 包含程序名 argv[1] 是路径 只有 argc 为 3 才访问 argv[2]
string_view 借用 argv 字符串比较选项 不持有新字符串 本次借用有效期覆盖比较
main 返回 inspect_file 的状态 0 正常 1 打开失败 2 参数错误或模拟业务失败

## Linux 操作和预期
所有命令在 Linux 虚拟机 ~/projects/netdisk 执行
```sh
cd "$HOME/projects/netdisk"
cmake -S . -B build
cmake --build build --parallel 2
./build/netdisk CMakeLists.txt
echo $?
./build/netdisk CMakeLists.txt --fail-after-open
echo $?
./build/netdisk
echo $?
./build/netdisk CMakeLists.txt --bad
echo $?
./build/netdisk CMakeLists.txt/child
echo $?
```
正常输出 opened fd=N 和 normal return 状态 0 N 不固定
模拟失败输出 opened fd=N 和模拟失败说明 状态 2 两个流混合顺序不作保证
无参数和非法选项状态 2
CMakeLists.txt 是普通文件 用其作为中间目录会失败 常见 ENOTDIR 状态 1
```sh
( ./build/netdisk CMakeLists.txt <&- )
echo $?
```
子 shell 只对该程序关闭标准输入 常见 fd 为 0 验证不能用 fd > 0 判断有效性

## 观察释放
若已安装 strace 使用
```sh
strace -e trace=openat,close ./build/netdisk CMakeLists.txt
strace -e trace=openat,close ./build/netdisk CMakeLists.txt --fail-after-open
```
定位目标路径对应 openat 返回的 fd 再找到其 close 返回 0
动态库加载也有 openat 和 close 不能数总行数下结论
open 经 libc 包装后在跟踪中常显示 openat
程序结束时内核也会回收 fd 所以仅看退出不证明析构执行 strace 中目标 close 提供证据
缺少 strace 时该观察项待确认 不假装已验证

## 小实验和答案
在 UniqueFd file(raw_fd) 后临时添加 UniqueFd another = file 应编译失败 因复制构造被删除 删除实验行再构建
禁止复制不能阻止手动 UniqueFd another(file.get()) 必须遵守一个 fd 只有一个 owner 的约定
故障分支不能手动 close 否则析构会重复关闭 fd 复用后可能误关别的资源
无需读完整文件即可学习所有权 下一天将此封装用于 socket
成功 open 目录在 Linux 上也可能发生 今天不声称验证普通文件类型

## 当前限制与验收
本地静态核对 不具备 Linux 实际执行证据 用户虚拟机修改 构建 测试 理解均待确认
不支持移动 release reset 不实现读写与 close 错误报告 不保证磁盘持久化
未进行异常栈展开故障注入 尚未测试真实 Socket
验收包含正常打开 打开失败 提前返回 禁止复制以及跟踪实际关闭
参考 https://man7.org/linux/man-pages/man2/open.2.html
参考 https://www.man7.org/linux/man-pages/man2/close.2.html

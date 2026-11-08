#include "socket_io.h"
#include "unique_fd.h"

#include <algorithm>
#include <cstdlib>
#include <csignal>
#include <string>

std::string captured;
int calls = 0;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

ssize_t interrupted_then_short(
    int, const void* data, std::size_t size, int flags)
{
    require((flags & MSG_NOSIGNAL) != 0, "MSG_NOSIGNAL missing");
    ++calls;
    if (calls == 1)
    {
        errno = EINTR;
        return -1;
    }
    const std::size_t n = std::min(size, std::size_t{2});
    captured.append(static_cast<const char*>(data), n);
    return static_cast<ssize_t>(n);
}

ssize_t return_zero(int, const void*, std::size_t, int)
{
    return 0;
}

ssize_t return_error(int, const void*, std::size_t, int)
{
    errno = EPIPE;
    return -1;
}

int main()
{
    const std::string payload("A\0BCD", 5);
    require(send_all(-1, payload.data(), payload.size(),
                     interrupted_then_short), "retry should succeed");
    require(captured == payload, "bytes duplicated or lost");
    require(calls == 4, "expected EINTR then 2 + 2 + 1 bytes");

    require(!send_all(-1, "x", 1, return_zero),
            "zero progress must fail");
    require(!send_all(-1, "x", 1, return_error),
            "EPIPE must fail");
    require(send_all(-1, "", 0, return_error),
            "empty send must succeed without calling sender");

    require(std::signal(SIGPIPE, SIG_DFL) != SIG_ERR,
            "cannot restore default SIGPIPE action");
    int pair[2];
    require(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0,
            "socketpair failed");
    UniqueFd sender(pair[0]);
    {
        UniqueFd peer(pair[1]);
    }

    require(!send_all(sender.get(), "x", 1),
            "sending to a closed peer must fail");
    std::cout << "PASS: retry, short writes, binary bytes, "
                 "zero progress and closed-peer send\n";
    return 0;
}

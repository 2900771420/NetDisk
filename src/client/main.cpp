#include <netdisk/client/download_session.h>

// ===== DAY 34 ADDED BEGIN 支持严格数值选项解析 =====
#include <charconv>
#include <cstdint>
#include <limits>
#include <string_view>
// ===== DAY 34 ADDED END =====
#include <iostream>
#include <string>

namespace
{

// ===== DAY 34 ADDED BEGIN 解析可配置端点与超时参数 =====
constexpr int max_timeout_ms = 600000;

void print_usage()
{
    std::cerr << "usage: netdisk_download [--host IPV4] [--port PORT] "
                 "[--timeout-ms MILLISECONDS] FILE_ID OUTPUT_PATH\n";
}

bool parse_unsigned(std::string_view text, unsigned int& value)
{
    if (text.empty()) return false;
    unsigned int parsed = 0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return false;
    value = parsed;
    return true;
}

bool parse_options(int argc, char* argv[],
                   download_client::DownloadOptions& options,
                   int& positional_index)
{
    bool host_seen = false;
    bool port_seen = false;
    bool timeout_seen = false;
    int index = 1;
    while (index < argc)
    {
        const std::string_view argument(argv[index]);
        if (argument == "--")
        {
            ++index;
            break;
        }
        if (argument.size() < 2 || argument[0] != '-' || argument[1] != '-')
            break;
        if (index + 1 >= argc)
        {
            std::cerr << "missing value for " << argument << '\n';
            return false;
        }
        const std::string_view value(argv[index + 1]);
        if (argument == "--host" && !host_seen)
        {
            if (value.empty())
            {
                std::cerr << "host must not be empty\n";
                return false;
            }
            options.server_ipv4 = std::string(value);
            host_seen = true;
        }
        else if (argument == "--port" && !port_seen)
        {
            unsigned int parsed = 0;
            if (!parse_unsigned(value, parsed) || parsed == 0 ||
                parsed > std::numeric_limits<std::uint16_t>::max())
            {
                std::cerr << "port must be between 1 and 65535\n";
                return false;
            }
            options.server_port = static_cast<std::uint16_t>(parsed);
            port_seen = true;
        }
        else if (argument == "--timeout-ms" && !timeout_seen)
        {
            unsigned int parsed = 0;
            if (!parse_unsigned(value, parsed) || parsed == 0 ||
                parsed > static_cast<unsigned int>(max_timeout_ms))
            {
                std::cerr << "timeout must be between 1 and 600000 ms\n";
                return false;
            }
            options.timeout_ms = static_cast<int>(parsed);
            timeout_seen = true;
        }
        else
        {
            std::cerr << "unknown or duplicate option: " << argument << '\n';
            return false;
        }
        index += 2;
    }
    positional_index = index;
    return true;
}
// ===== DAY 34 ADDED END =====

}

// ===== DAY 34 MODIFIED BEGIN 传递端点与超时配置到下载会话 =====
int main(int argc, char* argv[])
{
    download_client::DownloadOptions options;
    int positional_index = 1;
    if (!parse_options(argc, argv, options, positional_index) ||
        argc - positional_index != 2)
    {
        print_usage();
        return 2;
    }
    return run_download(argv[positional_index],
                        std::string(argv[positional_index + 1]), options);
}
// ===== DAY 34 MODIFIED END =====

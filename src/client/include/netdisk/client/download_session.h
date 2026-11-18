#pragma once

// ===== DAY 34 ADDED BEGIN 为端口配置引入固定宽度整数 =====
#include <cstdint>
// ===== DAY 34 ADDED END =====
#include <string>
#include <string_view>

// ===== DAY 34 ADDED BEGIN 定义可配置服务端与有限网络等待 =====
namespace download_client
{

struct DownloadOptions
{
    std::string server_ipv4 = "127.0.0.1";
    std::uint16_t server_port = 8080;
    int timeout_ms = 5000;
};

}
// ===== DAY 34 ADDED END =====

int run_download(std::string_view id, const std::string& output_path);

// ===== DAY 34 ADDED BEGIN 允许调用方传入下载选项 =====
int run_download(std::string_view id, const std::string& output_path,
                 const download_client::DownloadOptions& options);
// ===== DAY 34 ADDED END =====

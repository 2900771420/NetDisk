#include "unique_fd.h"
#include "socket_io.h"
#include "http_response.h"
#include "temp_upload.h"
#include "file_download.h"
#include "file_catalog.h"
#include "metadata_database.h"
#include "file_metadata_store.h"
#include "publishing_recovery.h"
#include "deletion_recovery.h"
#include "storage_audit.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>

bool send_response(int client_fd, const HttpResponse& response, bool head_only)
{
    const std::string wire = build_response(response, head_only);
    std::cout << "response status=" << response.status << '\n';
    return send_all(client_fd, wire.data(), wire.size());
}

bool is_upload(const RequestLine& line)
{
    const std::string_view target(line.target);
    const auto path = target.substr(0, target.find('?'));
    return line.method == "POST" && (path == "/dev/upload" || path == "/api/files");
}

bool handle_client(int client_fd, FileMetadataStore& metadata)
{
    TempUpload upload;
    RequestParser parser([&upload](const RequestLine& line, std::string_view chunk)
    {
        if (!is_upload(line)) return BodyAction::Buffer;
        return upload.append(chunk) ? BodyAction::Written : BodyAction::Failed;
    });
    ParseState state = ParseState::NeedMore;
    bool received_any = false;
    char buffer[1024];

    while (state == ParseState::NeedMore)
    {
        const ssize_t n = ::recv(client_fd, buffer, sizeof(buffer), 0);
        if (n == -1)
        {
            const int error = errno;
            if (error == EINTR)
            {
                continue;
            }
            print_system_error("recv", error);
            return false;
        }
        if (n == 0)
        {
            if (!received_any) return false;
            parser.finish();
            std::cerr << "peer closed before request completed\n";
            return send_response(client_fd, parser_error_response(parser.error()),
                                 parser.line().method == "HEAD");
        }

        received_any = true;
        const std::size_t count = static_cast<std::size_t>(n);
        const auto result = parser.feed(std::string_view(buffer, count));
        state = result.state;
        if (state == ParseState::Error)
        {
            std::cerr << "invalid or unsupported request or size limit exceeded\n";
            if (parser.error() == RequestError::BodyWriteFailed)
                print_system_error("temporary upload", upload.error());
            return send_response(client_fd, parser_error_response(parser.error()),
                                 parser.line().method == "HEAD");
        }
        if (result.consumed < count)
        {
            std::cout << "trailing bytes ignored before connection close\n";
        }
    }

    const RequestLine& line = parser.line();
    std::cout << "method=" << line.method
              << " target=" << line.target
              << " version=" << line.version << '\n';
    std::string_view file_id;
    if (match_file_content(line.target, file_id))
    {
        return send_file_content(client_fd, parser, file_id, metadata);
    }
    if (is_file_catalog_request(line))
        return send_response(client_fd, file_catalog_response(parser, metadata),
                             line.method == "HEAD");
    if (is_upload(line))
    {
        const std::string_view target(line.target);
        const bool permanent = target.substr(0, target.find('?')) == "/api/files";
        if (!permanent)
        {
            if (!upload.close_file())
            {
                print_system_error("finalize upload", upload.error());
                return send_response(client_fd, error_response(500), false);
            }
        }
        std::cout << "temporary bytes=" << parser.body_size()
                  << " buffered bytes=" << parser.body().size() << '\n';
        if (permanent)
        {
            if (!upload.prepare_publish(parser.body_size()))
            {
                print_system_error("prepare upload publication", upload.error());
                return send_response(client_fd, error_response(500), false);
            }
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const std::string default_name = upload.id() + ".bin";
            // ===== DAY 29 MODIFIED BEGIN 摘要与publishing元数据一起提交 =====
            const auto result = seconds < 0
                ? MetadataResult::Error
                : metadata.create_publishing(upload.id(), default_name, parser.body_size(),
                                             static_cast<std::int64_t>(seconds),
                                             upload.sha256());
            // ===== DAY 29 MODIFIED END =====
            if (result != MetadataResult::Ok)
            {
                std::cerr << "create publishing metadata failed: "
                          << (seconds < 0 ? "system clock is before Unix epoch"
                                          : metadata.error_message()) << '\n';
                return send_response(client_fd, error_response(500), false);
            }
            if (!upload.publish_prepared())
            {
                print_system_error("publish prepared object", upload.error());
                const auto cancelled = metadata.cancel_publishing(upload.id());
                if (cancelled != MetadataResult::Ok)
                    std::cerr << "cancel publishing metadata failed: "
                              << (cancelled == MetadataResult::NotFound
                                      ? "publishing row is missing"
                                      : metadata.error_message()) << '\n';
                return send_response(client_fd, error_response(500), false);
            }
            const auto ready = metadata.mark_ready(upload.id());
            if (ready != MetadataResult::Ok)
            {
                std::cerr << "mark published object ready failed: "
                          << (ready == MetadataResult::NotFound
                                  ? "publishing row is missing"
                                  : metadata.error_message())
                          << " leaving publishing state for recovery\n";
                return send_response(client_fd, error_response(500), false);
            }
            std::cout << "published ready id=" << upload.id() << '\n';
        }
    }
    // ===== DAY 29 MODIFIED BEGIN 上传成功同时返回文件ID和SHA256 =====
    return send_response(client_fd, route_request(parser, upload.id(), upload.sha256()),
                         line.method == "HEAD");
    // ===== DAY 29 MODIFIED END =====
}

int main()
{
    constexpr std::uint16_t port = 8080;
    constexpr int backlog = 16;

    MetadataDatabase metadata;
    if (!metadata.open("data/netdisk.db") || !metadata.initialize_schema())
    {
        std::cerr << "metadata database failed: " << metadata.error_message() << '\n';
        return 1;
    }
    // ===== DAY 29 MODIFIED BEGIN 输出迁移后的schema版本 =====
    std::cout << "metadata database ready path=data/netdisk.db schema=2\n";
    // ===== DAY 29 MODIFIED END =====

    FileMetadataStore file_metadata(metadata.get());
    if (!file_metadata.prepare())
    {
        std::cerr << "file metadata statements failed: "
                  << file_metadata.error_message() << '\n';
        return 1;
    }

    PublishingRecovery recovery(file_metadata);
    PublishingRecoveryReport recovery_report;
    if (!recovery.run(recovery_report))
    {
        std::cerr << "publishing recovery failed: "
                  << recovery.error_message() << '\n';
        return 1;
    }
    std::cout << "publishing recovery completed=" << recovery_report.completed
              << " cancelled=" << recovery_report.cancelled << '\n';

    DeletionRecovery deletion_recovery(file_metadata);
    DeletionRecoveryReport deletion_report;
    if (!deletion_recovery.run(deletion_report))
    {
        std::cerr << "deletion recovery failed: "
                  << deletion_recovery.error_message() << '\n';
        return 1;
    }
    std::cout << "deletion recovery completed=" << deletion_report.completed
              << " objects_removed=" << deletion_report.objects_removed << '\n';

    StorageAuditor storage_auditor(file_metadata);
    StorageAuditReport audit_report;
    if (!storage_auditor.run(audit_report))
    {
        std::cerr << "storage audit failed: "
                  << storage_auditor.error_message() << '\n';
        return 1;
    }
    std::cout << "storage audit ready=" << audit_report.ready_records
              << " disk_entries=" << audit_report.disk_entries
              << " healthy=" << audit_report.healthy_objects
              << " findings=" << audit_report.findings.size() << '\n';
    for (const StorageAuditFinding& finding : audit_report.findings)
    {
        std::cerr << "storage audit finding kind="
                  << storage_audit_kind_name(finding.kind)
                  << " id=" << escape_storage_audit_value(finding.id);
        if (finding.kind == StorageAuditFindingKind::SizeMismatch)
            std::cerr << " expected_size=" << finding.expected_size
                      << " actual_size=" << finding.actual_size;
        else if (finding.kind == StorageAuditFindingKind::MissingObject ||
                 finding.kind == StorageAuditFindingKind::NonRegularObject ||
                 finding.kind == StorageAuditFindingKind::InvalidMetadataId)
            std::cerr << " expected_size=" << finding.expected_size;
        std::cerr << '\n';
    }

    const int raw_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (raw_fd == -1)
    {
        const int error = errno;
        print_system_error("socket", error);
        return 1;
    }
    UniqueFd listener(raw_fd);

    const int reuse_address = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR,
                     &reuse_address, sizeof(reuse_address)) == -1)
    {
        const int error = errno;
        print_system_error("setsockopt", error);
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(listener.get(),
               reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) == -1)
    {
        const int error = errno;
        print_system_error("bind", error);
        return 1;
    }
    if (::listen(listener.get(), backlog) == -1)
    {
        const int error = errno;
        print_system_error("listen", error);
        return 1;
    }

    std::cout << "listening on 127.0.0.1:" << port
              << " fd=" << listener.get() << std::endl;

    while (true)
    {
        const int accepted_fd =
            ::accept(listener.get(), nullptr, nullptr);
        if (accepted_fd == -1)
        {
            const int error = errno;
            if (error == EINTR || error == ECONNABORTED)
            {
                continue;
            }
            print_system_error("accept", error);
            return 1;
        }

        UniqueFd client(accepted_fd);
        std::cout << "accepted fd=" << client.get() << std::endl;
        const bool ok = handle_client(client.get(), file_metadata);
        std::cout << (ok ? "response sent" : "connection failed")
                  << std::endl;
    }
}

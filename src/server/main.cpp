// ===== DAY 33A MODIFIED BEGIN main只负责启动编排与监听循环 =====
#include <netdisk/io/posix_io.h>
#include <netdisk/io/unique_fd.h>
#include <netdisk/maintenance/digest_backfill.h>
#include <netdisk/maintenance/recovery.h>
#include <netdisk/maintenance/storage_audit.h>
#include <netdisk/metadata/database.h>
#include <netdisk/metadata/file_metadata_store.h>
#include <netdisk/server/request_handler.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>

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
    std::cout << "metadata database ready path=data/netdisk.db schema=2\n";

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
        std::cerr << "publishing recovery failed: " << recovery.error_message() << '\n';
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

    DigestBackfiller digest_backfiller(file_metadata);
    DigestBackfillReport digest_report;
    if (!digest_backfiller.run(digest_report))
    {
        std::cerr << "digest backfill failed: "
                  << digest_backfiller.error_message() << '\n';
        return 1;
    }
    std::cout << "digest backfill ready=" << digest_report.ready_records
              << " already_hashed=" << digest_report.already_hashed
              << " backfilled=" << digest_report.backfilled
              << " skipped=" << digest_report.skipped << '\n';
    for (const DigestBackfillFinding& finding : digest_report.findings)
    {
        std::cerr << "digest backfill finding kind="
                  << digest_backfill_kind_name(finding.kind)
                  << " id=" << escape_storage_audit_value(finding.id)
                  << " expected_size=" << finding.expected_size;
        if (finding.kind == DigestBackfillFindingKind::SizeMismatch ||
            finding.kind == DigestBackfillFindingKind::ChangedDuringRead)
            std::cerr << " actual_size=" << finding.actual_size;
        std::cerr << '\n';
    }

    const int raw_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (raw_fd == -1)
    {
        print_system_error("socket", errno);
        return 1;
    }
    UniqueFd listener(raw_fd);

    const int reuse_address = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR,
                     &reuse_address, sizeof(reuse_address)) == -1)
    {
        print_system_error("setsockopt", errno);
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) == -1)
    {
        print_system_error("bind", errno);
        return 1;
    }
    if (::listen(listener.get(), backlog) == -1)
    {
        print_system_error("listen", errno);
        return 1;
    }

    std::cout << "listening on 127.0.0.1:" << port
              << " fd=" << listener.get() << std::endl;
    while (true)
    {
        const int accepted_fd = ::accept(listener.get(), nullptr, nullptr);
        if (accepted_fd == -1)
        {
            const int error = errno;
            if (error == EINTR || error == ECONNABORTED) continue;
            print_system_error("accept", error);
            return 1;
        }
        UniqueFd client(accepted_fd);
        std::cout << "accepted fd=" << client.get() << std::endl;
        const bool ok = handle_client(client.get(), file_metadata);
        std::cout << (ok ? "response sent" : "connection failed") << std::endl;
    }
}
// ===== DAY 33A MODIFIED END =====
// ===== DAY 33A DELETED 请求解析 路由 上传与下载实现移动到server实现文件 =====

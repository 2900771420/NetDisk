// ===== DAY 33A MODIFIED BEGIN 使用维护 HTTP与元数据模块公开接口 =====
#include <netdisk/maintenance/digest_backfill.h>
#include <netdisk/http/response.h>
#include <netdisk/metadata/database.h>
#include <netdisk/metadata/file_metadata_store.h>
// ===== DAY 33A MODIFIED END =====

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

struct TestDirectory
{
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("day30-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    TestDirectory()
    {
        require(std::filesystem::create_directory(root), "create isolated directory");
        std::filesystem::current_path(root);
        std::filesystem::create_directories("data/objects");
    }

    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::current_path(previous, error);
        std::filesystem::remove_all(root, error);
    }
};

void write_object(std::string_view id, std::string_view bytes)
{
    const std::filesystem::path path =
        std::filesystem::path("data/objects") / std::string(id);
    std::ofstream output(path, std::ios::binary);
    require(output.is_open(), "open object fixture");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(output.good(), "write object fixture");
}

void create_ready(FileMetadataStore& store, std::string_view id,
                  std::uint64_t size, std::int64_t created_at,
                  std::string_view digest = {})
{
    require(store.create_publishing(id, std::string(id) + ".bin", size,
                                    created_at, digest) == MetadataResult::Ok,
            "create ready fixture metadata");
    require(store.mark_ready(id) == MetadataResult::Ok,
            "mark fixture metadata ready");
}

int main()
{
    try
    {
        TestDirectory directory;
        MetadataDatabase database;
        require(database.open("data/netdisk.db"), "open metadata database");
        require(database.initialize_schema(), "initialize schema version 2");
        FileMetadataStore store(database.get());
        require(store.prepare(), "prepare metadata store");

        constexpr std::string_view legacy_id = "upload-Old030";
        constexpr std::string_view legacy_bytes("legacy\0day30", 12);
        constexpr std::string_view legacy_digest =
            "8af29b6187c9a99b7d99fc176b335e31"
            "a4266f023110d1545f512be0eccee842";
        constexpr std::string_view existing_digest =
            "ba7816bf8f01cfea414140de5dae2223"
            "b00361a396177a9cb410ff61f20015ad";

        write_object(legacy_id, legacy_bytes);
        create_ready(store, legacy_id, legacy_bytes.size(), 100);
        write_object("upload-New030", "abc");
        create_ready(store, "upload-New030", 3, 101, existing_digest);
        create_ready(store, "upload-Miss30", 4, 102);
        write_object("upload-Size30", "bad");
        create_ready(store, "upload-Size30", 4, 103);
        std::filesystem::create_directory("data/objects/upload-Dir030");
        create_ready(store, "upload-Dir030", 0, 104);
        create_ready(store, "../bad", 0, 105);

        DigestBackfiller backfiller(store);
        DigestBackfillReport first;
        require(backfiller.run(first), "first digest backfill");
        require(first.ready_records == 6 && first.already_hashed == 1 &&
                first.backfilled == 1 && first.skipped == 4 &&
                first.findings.size() == 4,
                "first backfill report differs");

        FileMetadata legacy;
        require(store.find_ready(legacy_id, legacy) == MetadataResult::Ok &&
                legacy.sha256 == legacy_digest,
                "legacy digest was not persisted");
        FileMetadata existing;
        require(store.find_ready("upload-New030", existing) == MetadataResult::Ok &&
                existing.sha256 == existing_digest,
                "existing digest was overwritten");
        require(store.set_sha256_if_missing("upload-New030", 3, legacy_digest) ==
                    MetadataResult::NotFound,
                "conditional backfill overwrote an existing digest");
        FileMetadata missing;
        require(store.find_ready("upload-Miss30", missing) == MetadataResult::Ok &&
                missing.sha256.empty(), "invalid object received a digest");

        DigestBackfillReport second;
        require(backfiller.run(second), "second digest backfill");
        require(second.ready_records == 6 && second.already_hashed == 2 &&
                second.backfilled == 0 && second.skipped == 4,
                "second backfill was not idempotent");
        std::cout << "PASS: valid legacy digest is backfilled once and anomalies stay unknown\n";

        std::string header;
        // ===== DAY 31 MODIFIED BEGIN 完整下载头显式传入空范围 =====
        require(make_download_header(
                    legacy_id, legacy.size, legacy.sha256, nullptr, header),
                "build ETag download header");
        require(header.find("ETag: \"" + legacy.sha256 + "\"\r\n") !=
                    std::string::npos,
                "strong ETag does not match persisted digest");
        header = "unchanged";
        require(!make_download_header(
                    legacy_id, legacy.size, {}, nullptr, header) &&
                header == "unchanged", "missing digest produced a download header");
        // ===== DAY 31 MODIFIED END =====
        require(store.set_sha256_if_missing(legacy_id, legacy.size,
                                            std::string(64, 'A')) ==
                    MetadataResult::Error,
                "noncanonical digest was accepted");
        std::cout << "PASS: download header requires the persisted canonical SHA256 ETag\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

// ===== DAY 25 ADDED BEGIN 验证publishing事务边界与公开可见性 =====
#include "file_catalog.h"
#include "metadata_database.h"

#ifdef __linux__
#include "file_download.h"
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

struct TestDirectory
{
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("day25-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    TestDirectory()
    {
        require(std::filesystem::create_directory(root), "create isolated directory");
        std::filesystem::current_path(root);
    }

    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::current_path(previous, error);
        std::filesystem::remove_all(root, error);
    }
};

std::string file_state(sqlite3* database, std::string_view id)
{
    sqlite3_stmt* statement = nullptr;
    require(sqlite3_prepare_v2(database,
                "SELECT state FROM files WHERE id = ?1;", -1, &statement, nullptr) ==
            SQLITE_OK, "prepare state query");
    const int bound = sqlite3_bind_text(statement, 1, id.data(),
        static_cast<int>(id.size()), SQLITE_TRANSIENT);
    require(bound == SQLITE_OK, "bind state query");
    const int result = sqlite3_step(statement);
    std::string state;
    if (result == SQLITE_ROW)
    {
        const auto* text = sqlite3_column_text(statement, 0);
        require(text != nullptr, "state text is null");
        state.assign(reinterpret_cast<const char*>(text),
                     static_cast<std::size_t>(sqlite3_column_bytes(statement, 0)));
        require(sqlite3_step(statement) == SQLITE_DONE, "duplicate state rows");
    }
    else
    {
        require(result == SQLITE_DONE, "read state query");
    }
    sqlite3_finalize(statement);
    return state;
}

HttpResponse detail_response(FileMetadataStore& store, std::string_view id)
{
    RequestParser parser;
    const std::string wire = "GET /api/files/" + std::string(id) +
        " HTTP/1.1\r\nHost: localhost\r\n\r\n";
    require(parser.feed(wire).state == ParseState::Done, "parse detail request");
    return file_catalog_response(parser, store);
}

#ifdef __linux__
std::string download_response(FileMetadataStore& store, std::string_view id)
{
    int sockets[2] = {-1, -1};
    require(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
            "create download socket pair");
    RequestParser parser;
    const std::string wire = "GET /api/files/" + std::string(id) +
        "/content HTTP/1.1\r\nHost: localhost\r\n\r\n";
    require(parser.feed(wire).state == ParseState::Done, "parse download request");
    require(send_file_content(sockets[0], parser, id, store), "send download response");
    require(::shutdown(sockets[0], SHUT_WR) == 0, "shutdown download writer");
    ::close(sockets[0]);
    sockets[0] = -1;

    std::string response;
    char buffer[1024];
    while (true)
    {
        const ssize_t received = ::read(sockets[1], buffer, sizeof(buffer));
        if (received == 0) break;
        require(received > 0, "read download response");
        response.append(buffer, static_cast<std::size_t>(received));
    }
    ::close(sockets[1]);
    return response;
}
#endif

int main()
{
    try
    {
        TestDirectory directory;
        std::filesystem::create_directories("data/objects");
        MetadataDatabase database;
        require(database.open("data/netdisk.db") && database.initialize_schema(),
                "initialize metadata database");
        FileMetadataStore store(database.get());
        require(store.prepare(), "prepare metadata statements");

        constexpr std::string_view id = "upload-State1";
        const std::string payload = "publishing-boundary";
        require(store.create_publishing(id, "state-one.bin", payload.size(), 100) ==
                    MetadataResult::Ok,
                "create publishing metadata");
        require(sqlite3_get_autocommit(database.get()) == 1 &&
                file_state(database.get(), id) == "publishing",
                "publishing transaction did not commit");
        require(store.create_publishing(id, "duplicate.bin", 1, 101) ==
                    MetadataResult::Conflict &&
                sqlite3_get_autocommit(database.get()) == 1 &&
                file_state(database.get(), id) == "publishing",
                "conflicting transaction changed state or remained open");

        {
            std::ofstream object(std::filesystem::path("data/objects") / id,
                                 std::ios::binary);
            object.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            require(object.good(), "write prepared object");
        }
        FileMetadata metadata;
        require(store.find_ready(id, metadata) == MetadataResult::NotFound &&
                list_file_objects(store).at("files").empty() &&
                detail_response(store, id).status == 404,
                "publishing metadata became visible in catalog");
#ifdef __linux__
        require(download_response(store, id).rfind("HTTP/1.1 404 Not Found\r\n", 0) == 0,
                "publishing object became downloadable");
#endif

        require(store.mark_ready(id) == MetadataResult::Ok &&
                sqlite3_get_autocommit(database.get()) == 1 &&
                file_state(database.get(), id) == "ready",
                "ready transaction did not commit");
        require(store.find_ready(id, metadata) == MetadataResult::Ok &&
                metadata.size == payload.size() &&
                list_file_objects(store).at("files").size() == 1 &&
                detail_response(store, id).status == 200,
                "ready metadata did not become visible");
#ifdef __linux__
        const std::string downloaded = download_response(store, id);
        const auto separator = downloaded.find("\r\n\r\n");
        require(downloaded.rfind("HTTP/1.1 200 OK\r\n", 0) == 0 &&
                separator != std::string::npos &&
                downloaded.substr(separator + 4) == payload,
                "ready object download differs");
#endif
        require(store.mark_ready(id) == MetadataResult::NotFound &&
                store.cancel_publishing(id) == MetadataResult::NotFound &&
                file_state(database.get(), id) == "ready",
                "invalid transition changed ready metadata");

        constexpr std::string_view cancelled = "upload-State2";
        require(store.create_publishing(cancelled, "state-two.bin", 0, 102) ==
                    MetadataResult::Ok &&
                store.cancel_publishing(cancelled) == MetadataResult::Ok &&
                sqlite3_get_autocommit(database.get()) == 1 &&
                file_state(database.get(), cancelled).empty(),
                "cancel transaction left publishing metadata");
        require(store.create_publishing("upload-State3", "too-large.bin",
                    std::numeric_limits<std::uint64_t>::max(), 103) ==
                    MetadataResult::Error &&
                sqlite3_get_autocommit(database.get()) == 1,
                "invalid size opened a transaction");

        std::cout << "PASS: publishing transactions hide incomplete objects until ready\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
// ===== DAY 25 ADDED END =====

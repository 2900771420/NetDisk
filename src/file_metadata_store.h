#pragma once

#include <sqlite3.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct FileMetadata
{
    std::string id;
    std::string name;
    std::uint64_t size = 0;
    std::int64_t created_at = 0;
};

struct FilePageRequest
{
    std::size_t limit = 100;
    bool has_cursor = false;
    std::int64_t cursor_created_at = 0;
    std::string cursor_id;
};

enum class MetadataResult
{
    Ok,
    NotFound,
    Conflict,
    Error
};

class FileMetadataStore
{
public:
    explicit FileMetadataStore(sqlite3* database) noexcept
        : database_(database)
    {
    }

    FileMetadataStore(const FileMetadataStore&) = delete;
    FileMetadataStore& operator=(const FileMetadataStore&) = delete;

    ~FileMetadataStore() noexcept
    {
        sqlite3_finalize(create_);
        sqlite3_finalize(ready_);
        sqlite3_finalize(cancel_);
        sqlite3_finalize(list_publishing_);
        // ===== DAY 27 ADDED BEGIN 释放删除状态机与恢复查询语句 =====
        sqlite3_finalize(mark_deleting_);
        sqlite3_finalize(erase_deleting_);
        sqlite3_finalize(list_deleting_);
        // ===== DAY 27 ADDED END =====
        sqlite3_finalize(list_);
        sqlite3_finalize(find_);
        sqlite3_finalize(rename_);
    }

    bool prepare()
    {
        if (database_ == nullptr)
        {
            remember_error("prepare file metadata statements", SQLITE_MISUSE,
                           "database connection is null");
            return false;
        }
        if (prepared_)
        {
            remember_error("prepare file metadata statements", SQLITE_MISUSE,
                           "statements are already prepared");
            return false;
        }

        if (!prepare_statement(create_,
                "INSERT INTO files(id, name, size, state, created_at) "
                "VALUES(?1, ?2, ?3, 'publishing', ?4);",
                "prepare create publishing file") ||
            !prepare_statement(ready_,
                "UPDATE files SET state = 'ready' "
                "WHERE id = ?1 AND state = 'publishing';",
                "prepare mark file ready") ||
            !prepare_statement(cancel_,
                "DELETE FROM files WHERE id = ?1 AND state = 'publishing';",
                "prepare cancel publishing file") ||
            !prepare_statement(list_publishing_,
                "SELECT id, name, size, created_at FROM files "
                "WHERE state = 'publishing' ORDER BY created_at, id;",
                "prepare list publishing files") ||
            // ===== DAY 27 DELETED 删除直接擦除ready记录的预编译语句 改用deleting状态机 =====
            // ===== DAY 27 ADDED BEGIN 准备删除状态转换与启动恢复查询 =====
            !prepare_statement(mark_deleting_,
                "UPDATE files SET state = 'deleting' "
                "WHERE id = ?1 AND state = 'ready';",
                "prepare mark file deleting") ||
            !prepare_statement(erase_deleting_,
                "DELETE FROM files WHERE id = ?1 AND state = 'deleting';",
                "prepare erase deleting file") ||
            !prepare_statement(list_deleting_,
                "SELECT id, name, size, created_at FROM files "
                "WHERE state = 'deleting' ORDER BY created_at, id;",
                "prepare list deleting files") ||
            // ===== DAY 27 ADDED END =====
            !prepare_statement(list_,
                "SELECT id, name, size, created_at FROM files "
                "WHERE state = 'ready' AND "
                "(?1 = 0 OR created_at > ?2 OR (created_at = ?2 AND id > ?3)) "
                "ORDER BY created_at, id LIMIT ?4;", "prepare list files") ||
            !prepare_statement(find_,
                "SELECT id, name, size, created_at FROM files "
                "WHERE id = ?1 AND state = 'ready';", "prepare find file") ||
            !prepare_statement(rename_,
                "UPDATE files SET name = ?1 WHERE id = ?2 AND state = 'ready';",
                "prepare rename file"))
            return false;

        prepared_ = true;
        clear_error();
        return true;
    }

    MetadataResult create_publishing(std::string_view id, std::string_view name,
                                     std::uint64_t size, std::int64_t created_at)
    {
        if (!start_statement(create_, "create publishing metadata"))
            return MetadataResult::Error;
        if (size > static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()) ||
            created_at < 0)
        {
            remember_error("create publishing metadata", SQLITE_TOOBIG,
                           "size or creation time is outside SQLite INTEGER range");
            return MetadataResult::Error;
        }
        if (!begin_transaction("begin publishing metadata transaction"))
            return MetadataResult::Error;

        bool bound = false;
        int result = SQLITE_ERROR;
        {
            StatementReset reset(create_);
            bound = bind_text(create_, 1, id, "bind publishing file id") &&
                bind_text(create_, 2, name, "bind publishing file name") &&
                bind_integer(create_, 3, static_cast<sqlite3_int64>(size),
                             "bind publishing file size") &&
                bind_integer(create_, 4, static_cast<sqlite3_int64>(created_at),
                             "bind publishing creation time");
            if (bound) result = sqlite3_step(create_);
        }
        if (!bound)
        {
            rollback_transaction();
            return MetadataResult::Error;
        }
        if (result != SQLITE_DONE)
        {
            remember_error("create publishing metadata", result, sqlite3_errmsg(database_));
            rollback_transaction();
            return primary_code(result) == SQLITE_CONSTRAINT
                ? MetadataResult::Conflict : MetadataResult::Error;
        }
        if (!commit_transaction("commit publishing metadata transaction"))
        {
            rollback_transaction();
            return MetadataResult::Error;
        }
        clear_error();
        return MetadataResult::Ok;
    }

    // ===== DAY 27 MODIFIED BEGIN 发布状态转换改用通用事务模板 =====
    MetadataResult mark_ready(std::string_view id)
    {
        return change_state(ready_, id, "mark file ready",
                            "begin publishing state transaction",
                            "commit ready metadata transaction");
    }

    MetadataResult cancel_publishing(std::string_view id)
    {
        return change_state(cancel_, id, "cancel publishing file",
                            "begin publishing state transaction",
                            "commit cancel metadata transaction");
    }
    // ===== DAY 27 MODIFIED END =====

    MetadataResult list_publishing(std::vector<FileMetadata>& output)
    {
        output.clear();
        if (!start_statement(list_publishing_, "list publishing metadata"))
            return MetadataResult::Error;
        StatementReset reset(list_publishing_);
        while (true)
        {
            const int result = sqlite3_step(list_publishing_);
            if (result == SQLITE_DONE)
            {
                clear_error();
                return MetadataResult::Ok;
            }
            if (result != SQLITE_ROW)
            {
                remember_error("list publishing metadata", result,
                               sqlite3_errmsg(database_));
                output.clear();
                return MetadataResult::Error;
            }
            FileMetadata file;
            if (!read_row(list_publishing_, file, "read publishing metadata"))
            {
                output.clear();
                return MetadataResult::Error;
            }
            output.push_back(std::move(file));
        }
    }

    // ===== DAY 27 ADDED BEGIN 提交删除意图并只读取deleting记录供恢复 =====
    MetadataResult mark_deleting(std::string_view id)
    {
        return change_state(mark_deleting_, id, "mark file deleting",
                            "begin deleting state transaction",
                            "commit deleting state transaction");
    }

    MetadataResult erase_deleting(std::string_view id)
    {
        return change_state(erase_deleting_, id, "erase deleting file",
                            "begin erase deleting transaction",
                            "commit erase deleting transaction");
    }

    MetadataResult list_deleting(std::vector<FileMetadata>& output)
    {
        output.clear();
        if (!start_statement(list_deleting_, "list deleting metadata"))
            return MetadataResult::Error;
        StatementReset reset(list_deleting_);
        while (true)
        {
            const int result = sqlite3_step(list_deleting_);
            if (result == SQLITE_DONE)
            {
                clear_error();
                return MetadataResult::Ok;
            }
            if (result != SQLITE_ROW)
            {
                remember_error("list deleting metadata", result,
                               sqlite3_errmsg(database_));
                output.clear();
                return MetadataResult::Error;
            }
            FileMetadata file;
            if (!read_row(list_deleting_, file, "read deleting metadata"))
            {
                output.clear();
                return MetadataResult::Error;
            }
            output.push_back(std::move(file));
        }
    }
    // ===== DAY 27 ADDED END =====

    MetadataResult list_ready(const FilePageRequest& page,
                              std::vector<FileMetadata>& output, bool& has_more)
    {
        output.clear();
        has_more = false;
        if (!start_statement(list_, "list file metadata"))
            return MetadataResult::Error;
        StatementReset reset(list_);
        if (page.limit == 0 || page.limit > 100 ||
            (page.has_cursor && page.cursor_id.empty()))
        {
            remember_error("list file metadata", SQLITE_RANGE,
                           "page limit or cursor is invalid");
            return MetadataResult::Error;
        }
        if (!bind_integer(list_, 1, page.has_cursor ? 1 : 0, "bind cursor presence") ||
            !bind_integer(list_, 2, page.cursor_created_at, "bind cursor creation time") ||
            !bind_text(list_, 3, page.cursor_id, "bind cursor file id") ||
            !bind_integer(list_, 4, static_cast<sqlite3_int64>(page.limit + 1),
                          "bind page row limit"))
            return MetadataResult::Error;
        while (true)
        {
            const int result = sqlite3_step(list_);
            if (result == SQLITE_DONE)
            {
                if (output.size() > page.limit)
                {
                    output.pop_back();
                    has_more = true;
                }
                clear_error();
                return MetadataResult::Ok;
            }
            if (result != SQLITE_ROW)
            {
                remember_error("list file metadata", result, sqlite3_errmsg(database_));
                output.clear();
                return MetadataResult::Error;
            }
            FileMetadata file;
            if (!read_row(list_, file, "read listed file"))
            {
                output.clear();
                return MetadataResult::Error;
            }
            output.push_back(std::move(file));
        }
    }

    MetadataResult find_ready(std::string_view id, FileMetadata& output)
    {
        if (!start_statement(find_, "find file metadata"))
            return MetadataResult::Error;
        StatementReset reset(find_);
        if (!bind_text(find_, 1, id, "bind searched file id"))
            return MetadataResult::Error;

        const int result = sqlite3_step(find_);
        if (result == SQLITE_DONE)
        {
            clear_error();
            return MetadataResult::NotFound;
        }
        if (result != SQLITE_ROW)
        {
            remember_error("find file metadata", result, sqlite3_errmsg(database_));
            return MetadataResult::Error;
        }
        if (!read_row(find_, output, "read found file"))
            return MetadataResult::Error;
        const int trailing = sqlite3_step(find_);
        if (trailing != SQLITE_DONE)
        {
            remember_error("find file metadata", trailing,
                           trailing == SQLITE_ROW ? "file id returned multiple rows"
                                                  : sqlite3_errmsg(database_));
            return MetadataResult::Error;
        }
        clear_error();
        return MetadataResult::Ok;
    }

    MetadataResult rename_ready(std::string_view id, std::string_view name)
    {
        if (!start_statement(rename_, "rename file metadata"))
            return MetadataResult::Error;
        StatementReset reset(rename_);
        if (!bind_text(rename_, 1, name, "bind new file name") ||
            !bind_text(rename_, 2, id, "bind renamed file id"))
            return MetadataResult::Error;

        const int result = sqlite3_step(rename_);
        if (result != SQLITE_DONE)
        {
            remember_error("rename file metadata", result, sqlite3_errmsg(database_));
            return primary_code(result) == SQLITE_CONSTRAINT
                ? MetadataResult::Conflict : MetadataResult::Error;
        }
        clear_error();
        return sqlite3_changes(database_) == 0
            ? MetadataResult::NotFound : MetadataResult::Ok;
    }

    // ===== DAY 27 DELETED 删除erase_ready接口 防止绕过deleting状态直接删除公开元数据 =====

    int error_code() const noexcept
    {
        return error_code_;
    }

    const std::string& error_message() const noexcept
    {
        return error_message_;
    }

private:
    class StatementReset
    {
    public:
        explicit StatementReset(sqlite3_stmt* statement) noexcept
            : statement_(statement)
        {
        }

        ~StatementReset() noexcept
        {
            sqlite3_reset(statement_);
            sqlite3_clear_bindings(statement_);
        }

    private:
        sqlite3_stmt* statement_;
    };

    static int primary_code(int code) noexcept
    {
        return code & 0xff;
    }

    bool prepare_statement(sqlite3_stmt*& statement, const char* sql,
                           const char* operation)
    {
        const int result = sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr);
        if (result == SQLITE_OK) return true;
        remember_error(operation, result, sqlite3_errmsg(database_));
        return false;
    }

    bool start_statement(sqlite3_stmt* statement, const char* operation)
    {
        if (!prepared_ || statement == nullptr)
        {
            remember_error(operation, SQLITE_MISUSE, "statements are not prepared");
            return false;
        }
        sqlite3_reset(statement);
        const int result = sqlite3_clear_bindings(statement);
        if (result == SQLITE_OK) return true;
        remember_error(operation, result, sqlite3_errmsg(database_));
        return false;
    }

    bool bind_text(sqlite3_stmt* statement, int index, std::string_view value,
                   const char* operation)
    {
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            remember_error(operation, SQLITE_TOOBIG, "text value is too large");
            return false;
        }
        const int result = sqlite3_bind_text(
            statement, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        if (result == SQLITE_OK) return true;
        remember_error(operation, result, sqlite3_errmsg(database_));
        return false;
    }

    bool bind_integer(sqlite3_stmt* statement, int index, sqlite3_int64 value,
                      const char* operation)
    {
        const int result = sqlite3_bind_int64(statement, index, value);
        if (result == SQLITE_OK) return true;
        remember_error(operation, result, sqlite3_errmsg(database_));
        return false;
    }

    bool execute_command(const char* sql, const char* operation)
    {
        char* detail = nullptr;
        const int result = sqlite3_exec(database_, sql, nullptr, nullptr, &detail);
        if (result == SQLITE_OK)
        {
            sqlite3_free(detail);
            return true;
        }
        remember_error(operation, result,
                       detail == nullptr ? sqlite3_errmsg(database_) : detail);
        sqlite3_free(detail);
        return false;
    }

    bool begin_transaction(const char* operation)
    {
        return execute_command("BEGIN IMMEDIATE;", operation);
    }

    bool commit_transaction(const char* operation)
    {
        return execute_command("COMMIT;", operation);
    }

    void rollback_transaction() noexcept
    {
        sqlite3_exec(database_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }

    // ===== DAY 27 MODIFIED BEGIN 复用事务模板处理发布和删除状态转换 =====
    MetadataResult change_state(sqlite3_stmt* statement,
                                std::string_view id,
                                const char* operation,
                                const char* begin_operation,
                                const char* commit_operation)
    {
        if (!start_statement(statement, operation)) return MetadataResult::Error;
        if (!begin_transaction(begin_operation))
            return MetadataResult::Error;

        bool bound = false;
        int result = SQLITE_ERROR;
        int changed = 0;
        {
            StatementReset reset(statement);
            bound = bind_text(statement, 1, id, "bind state transition file id");
            if (bound)
            {
                result = sqlite3_step(statement);
                if (result == SQLITE_DONE) changed = sqlite3_changes(database_);
            }
        }
        if (!bound)
        {
            rollback_transaction();
            return MetadataResult::Error;
        }
        if (result != SQLITE_DONE)
        {
            remember_error(operation, result, sqlite3_errmsg(database_));
            rollback_transaction();
            return MetadataResult::Error;
        }
        if (changed != 1)
        {
            rollback_transaction();
            clear_error();
            return MetadataResult::NotFound;
        }
        if (!commit_transaction(commit_operation))
        {
            rollback_transaction();
            return MetadataResult::Error;
        }
        clear_error();
        return MetadataResult::Ok;
    }
    // ===== DAY 27 MODIFIED END =====

    bool read_row(sqlite3_stmt* statement, FileMetadata& output,
                  const char* operation)
    {
        if (sqlite3_column_type(statement, 0) != SQLITE_TEXT ||
            sqlite3_column_type(statement, 1) != SQLITE_TEXT ||
            sqlite3_column_type(statement, 2) != SQLITE_INTEGER ||
            sqlite3_column_type(statement, 3) != SQLITE_INTEGER)
        {
            remember_error(operation, SQLITE_MISMATCH, "metadata column type mismatch");
            return false;
        }
        const auto* id = sqlite3_column_text(statement, 0);
        const auto* name = sqlite3_column_text(statement, 1);
        const int id_size = sqlite3_column_bytes(statement, 0);
        const int name_size = sqlite3_column_bytes(statement, 1);
        const sqlite3_int64 size = sqlite3_column_int64(statement, 2);
        const sqlite3_int64 created_at = sqlite3_column_int64(statement, 3);
        if (id == nullptr || name == nullptr || id_size <= 0 || name_size <= 0 ||
            size < 0 || created_at < 0)
        {
            remember_error(operation, SQLITE_CORRUPT, "invalid metadata row");
            return false;
        }
        output.id.assign(reinterpret_cast<const char*>(id), static_cast<std::size_t>(id_size));
        output.name.assign(reinterpret_cast<const char*>(name),
                           static_cast<std::size_t>(name_size));
        output.size = static_cast<std::uint64_t>(size);
        output.created_at = static_cast<std::int64_t>(created_at);
        return true;
    }

    void remember_error(const char* operation, int code, const char* detail)
    {
        error_code_ = code;
        error_message_ = operation;
        error_message_ += ": ";
        error_message_ += detail == nullptr ? sqlite3_errstr(code) : detail;
    }

    void clear_error()
    {
        error_code_ = SQLITE_OK;
        error_message_.clear();
    }

    sqlite3* database_ = nullptr;
    sqlite3_stmt* create_ = nullptr;
    sqlite3_stmt* ready_ = nullptr;
    sqlite3_stmt* cancel_ = nullptr;
    sqlite3_stmt* list_publishing_ = nullptr;
    // ===== DAY 27 ADDED BEGIN 持有删除状态机与恢复查询语句 =====
    sqlite3_stmt* mark_deleting_ = nullptr;
    sqlite3_stmt* erase_deleting_ = nullptr;
    sqlite3_stmt* list_deleting_ = nullptr;
    // ===== DAY 27 ADDED END =====
    sqlite3_stmt* list_ = nullptr;
    sqlite3_stmt* find_ = nullptr;
    sqlite3_stmt* rename_ = nullptr;
    bool prepared_ = false;
    int error_code_ = SQLITE_OK;
    std::string error_message_;
};

#include "admin_dao.h"

#include <sqlite3.h>
#include <utility>

namespace srms {
namespace {

int bindText(sqlite3_stmt* stmt, int index, const std::string& value) {
    return sqlite3_bind_text(
        stmt, index, value.c_str(),
        static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

std::string columnText(sqlite3_stmt* stmt, int column) {
    const unsigned char* value = sqlite3_column_text(stmt, column);
    return value ? reinterpret_cast<const char*>(value) : "";
}

} // namespace

Result<std::optional<AdminAccount>> AdminDAO::findByUsername(
    const std::string& username) {

    sqlite3* db = database_.handle();
    if (!db) return {Status::DB_ERROR, std::nullopt, "database is not open"};

    const char* sql =
        "SELECT username, password_hash, salt, failed_attempts, "
        "locked_until, must_change_password "
        "FROM admins WHERE username = ?1 COLLATE NOCASE LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return {Status::DB_ERROR, std::nullopt, "admin query failed"};

    if (bindText(stmt, 1, username) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return {Status::DB_ERROR, std::nullopt, "admin query binding failed"};
    }

    int rc = sqlite3_step(stmt);

    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return {Status::OK, std::nullopt, ""};
    }

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return {Status::DB_ERROR, std::nullopt, "admin query failed"};
    }

    AdminAccount account;
    account.username = columnText(stmt, 0);
    account.password_hash = columnText(stmt, 1);
    account.salt = columnText(stmt, 2);
    account.failed_attempts = sqlite3_column_int(stmt, 3);
    account.locked_until = static_cast<std::time_t>(
        sqlite3_column_int64(stmt, 4));
    account.must_change_password = sqlite3_column_int(stmt, 5) != 0;

    rc = sqlite3_finalize(stmt);
    if (rc != SQLITE_OK)
        return {Status::DB_ERROR, std::nullopt, "admin query finalization failed"};

    return {Status::OK, std::optional<AdminAccount>{std::move(account)}, ""};
}

Status AdminDAO::updateLoginState(
    const std::string& username,
    int failed_attempts,
    std::time_t locked_until) {

    sqlite3* db = database_.handle();
    if (!db) return Status::DB_ERROR;

    const char* sql =
        "UPDATE admins SET failed_attempts = ?1, locked_until = ?2 "
        "WHERE username = ?3 COLLATE NOCASE;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return Status::DB_ERROR;

    int rc = sqlite3_bind_int(stmt, 1, failed_attempts);
    if (rc == SQLITE_OK)
        rc = sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(locked_until));
    if (rc == SQLITE_OK)
        rc = bindText(stmt, 3, username);
    if (rc == SQLITE_OK)
        rc = sqlite3_step(stmt);

    int changed = rc == SQLITE_DONE ? sqlite3_changes(db) : 0;
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) return Status::DB_ERROR;
    return changed > 0 ? Status::OK : Status::NOT_FOUND;
}

Status AdminDAO::updatePassword(
    const std::string& username,
    const std::string& password_hash,
    const std::string& salt,
    bool must_change_password) {

    sqlite3* db = database_.handle();
    if (!db) return Status::DB_ERROR;

    const char* sql =
        "UPDATE admins SET password_hash = ?1, salt = ?2, "
        "must_change_password = ?3 "
        "WHERE username = ?4 COLLATE NOCASE;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return Status::DB_ERROR;

    int rc = bindText(stmt, 1, password_hash);
    if (rc == SQLITE_OK) rc = bindText(stmt, 2, salt);
    if (rc == SQLITE_OK)
        rc = sqlite3_bind_int(stmt, 3, must_change_password ? 1 : 0);
    if (rc == SQLITE_OK) rc = bindText(stmt, 4, username);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);

    int changed = rc == SQLITE_DONE ? sqlite3_changes(db) : 0;
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) return Status::DB_ERROR;
    return changed > 0 ? Status::OK : Status::NOT_FOUND;
}

} // namespace srms

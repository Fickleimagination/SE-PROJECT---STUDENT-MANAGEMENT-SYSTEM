
#include "database.h"

#include <fstream>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace srms {
namespace {

bool execute(sqlite3* db, const std::string& sql) {
    char* error_message = nullptr;

    const int rc = sqlite3_exec(
        db,
        sql.c_str(),
        nullptr,
        nullptr,
        &error_message
    );

    sqlite3_free(error_message);
    return rc == SQLITE_OK;
}

bool readFile(
    const std::string& path,
    std::string& contents
) {
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    if (file.bad()) {
        return false;
    }

    contents = buffer.str();
    return !contents.empty();
}

bool integrityCheck(sqlite3* db) {
    sqlite3_stmt* statement = nullptr;

    const int rc = sqlite3_prepare_v2(
        db,
        "PRAGMA integrity_check;",
        -1,
        &statement,
        nullptr
    );

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return false;
    }

    int step_rc = sqlite3_step(statement);
    bool valid = false;

    if (step_rc == SQLITE_ROW) {
        const unsigned char* value =
            sqlite3_column_text(statement, 0);

        valid = value != nullptr &&
            std::string(
                reinterpret_cast<const char*>(value)
            ) == "ok";

        if (valid) {
            step_rc = sqlite3_step(statement);
            valid = step_rc == SQLITE_DONE;
        }
    }

    const int finalize_rc = sqlite3_finalize(statement);

    return valid && finalize_rc == SQLITE_OK;
}

}  // namespace

Database::~Database() {
    close();
}

void Database::close() noexcept {
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

bool Database::open(
    const std::string& db_path,
    const std::string& schema_path
) {
    close();

    const int rc = sqlite3_open_v2(
        db_path.c_str(),
        &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
        nullptr
    );

    if (rc != SQLITE_OK) {
        close();
        return false;
    }

    if (sqlite3_busy_timeout(db_, 2000) != SQLITE_OK) {
        close();
        return false;
    }

#ifndef _WIN32
    if (::chmod(db_path.c_str(), S_IRUSR | S_IWUSR) != 0) {
        close();
        return false;
    }
#endif

    if (!execute(db_, "PRAGMA foreign_keys = ON;") ||
        !execute(db_, "PRAGMA journal_mode = WAL;")) {
        close();
        return false;
    }

    std::string schema;

    if (!readFile(schema_path, schema)) {
        close();
        return false;
    }

    // Initialize the schema atomically.
    if (!execute(db_, "BEGIN IMMEDIATE;")) {
        close();
        return false;
    }

    if (!execute(db_, schema)) {
        execute(db_, "ROLLBACK;");
        close();
        return false;
    }

    if (!execute(db_, "COMMIT;")) {
        execute(db_, "ROLLBACK;");
        close();
        return false;
    }

    if (!integrityCheck(db_)) {
        close();
        return false;
    }

    return true;
}

}  // namespace srms

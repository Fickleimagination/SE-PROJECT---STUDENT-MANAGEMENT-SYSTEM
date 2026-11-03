#pragma once

#include <sqlite3.h>
#include <string>

namespace srms {

class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(
        const std::string& db_path,
        const std::string& schema_path
    );

    void close() noexcept;

    sqlite3* handle() const noexcept {
        return db_;
    }

private:
    sqlite3* db_ = nullptr;
};

}  // namespace srms
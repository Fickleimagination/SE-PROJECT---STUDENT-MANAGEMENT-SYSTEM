#include "database.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr const char* kDbPath =
    "build/database_error_test.db";

constexpr const char* kBadSchemaPath =
    "build/database_bad_schema_test.sql";

void cleanupFiles() {
    std::filesystem::create_directories("build");
    std::remove(kDbPath);
    std::remove("build/database_error_test.db-wal");
    std::remove("build/database_error_test.db-shm");
    std::remove(kBadSchemaPath);
}

bool tableExists(sqlite3* db, const char* tableName) {
    sqlite3_stmt* statement = nullptr;

    const char* sql =
        "SELECT 1 FROM sqlite_master "
        "WHERE type = 'table' AND name = ?1;";

    if (sqlite3_prepare_v2(
            db, sql, -1, &statement, nullptr
        ) != SQLITE_OK) {
        sqlite3_finalize(statement);
        return false;
    }

    sqlite3_bind_text(
        statement, 1, tableName, -1, SQLITE_TRANSIENT
    );

    const int rc = sqlite3_step(statement);
    const bool found = rc == SQLITE_ROW;

    const int finalize_rc = sqlite3_finalize(statement);

    return finalize_rc == SQLITE_OK && found;
}

TEST(DatabaseTest, MissingSchemaFileFailsAndClosesHandle) {
    cleanupFiles();

    srms::Database database;

    EXPECT_FALSE(database.open(
        kDbPath,
        "sql/file_that_does_not_exist.sql"
    ));

    EXPECT_EQ(database.handle(), nullptr);

    cleanupFiles();
}

TEST(DatabaseTest, FailedSchemaRollsBackPartialCreation) {
    cleanupFiles();

    {
        std::ofstream schema(kBadSchemaPath);

        ASSERT_TRUE(schema.is_open());

        schema
            << "CREATE TABLE partial_table (id INTEGER);"
            << "\nTHIS IS NOT VALID SQL;\n";
    }

    {
        srms::Database database;

        EXPECT_FALSE(database.open(
            kDbPath, kBadSchemaPath
        ));

        EXPECT_EQ(database.handle(), nullptr);
    }

    // Opening the same database with the valid schema should work.
    {
        srms::Database database;

        ASSERT_TRUE(database.open(
            kDbPath, "sql/schema.sql"
        ));

        EXPECT_FALSE(
            tableExists(database.handle(), "partial_table")
        );

        EXPECT_TRUE(
            tableExists(database.handle(), "students")
        );
        EXPECT_TRUE(
            tableExists(database.handle(), "admins")
        );
        EXPECT_TRUE(
            tableExists(database.handle(), "audit_log")
        );
    }

    cleanupFiles();
}

}  // namespace
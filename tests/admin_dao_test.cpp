#include "admin_dao.h"

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <string>

namespace srms {
namespace {

class AdminDAOTest : public ::testing::Test {
protected:
    Database db;
    AdminDAO* dao = nullptr;
    std::string db_path;

    void SetUp() override {
        const auto id =
            std::chrono::steady_clock::now().time_since_epoch().count();

        db_path = (std::filesystem::temp_directory_path() /
            ("srms_admin_dao_" + std::to_string(id) + ".sqlite")).string();

        ASSERT_TRUE(db.open(db_path, "sql/schema.sql"));
        dao = new AdminDAO(db);

        const char* sql =
            "INSERT INTO admins "
            "(username, password_hash, salt, role, failed_attempts, "
            "locked_until, must_change_password) "
            "VALUES ('admin1', 'oldhash', 'oldsalt', 'admin', 0, 0, 1);";

        ASSERT_EQ(sqlite3_exec(db.handle(), sql, nullptr, nullptr, nullptr),
                  SQLITE_OK);
    }

    void TearDown() override {
        delete dao;
        dao = nullptr;
        db.close();

        std::error_code error;
        std::filesystem::remove(db_path, error);
        std::filesystem::remove(db_path + "-wal", error);
        std::filesystem::remove(db_path + "-shm", error);
    }
};

TEST_F(AdminDAOTest, FindsExistingAdmin) {
    auto result = dao->findByUsername("ADMIN1");
    ASSERT_EQ(result.status, Status::OK);
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(result.value->username, "admin1");
    EXPECT_EQ(result.value->password_hash, "oldhash");
}

TEST_F(AdminDAOTest, ReturnsEmptyForMissingAdmin) {
    auto result = dao->findByUsername("missing");
    EXPECT_EQ(result.status, Status::OK);
    EXPECT_FALSE(result.value.has_value());
}

TEST_F(AdminDAOTest, UpdatesLoginState) {
    EXPECT_EQ(dao->updateLoginState("admin1", 3, 123456), Status::OK);

    auto result = dao->findByUsername("admin1");
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(result.value->failed_attempts, 3);
    EXPECT_EQ(result.value->locked_until, 123456);
}

TEST_F(AdminDAOTest, UpdatesPassword) {
    EXPECT_EQ(
        dao->updatePassword("admin1", "newhash", "newsalt", false),
        Status::OK);

    auto result = dao->findByUsername("admin1");
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(result.value->password_hash, "newhash");
    EXPECT_EQ(result.value->salt, "newsalt");
    EXPECT_FALSE(result.value->must_change_password);
}

TEST_F(AdminDAOTest, UpdateMissingAdminReturnsNotFound) {
    EXPECT_EQ(dao->updateLoginState("missing", 1, 0), Status::NOT_FOUND);
}

} // namespace
} // namespace srms

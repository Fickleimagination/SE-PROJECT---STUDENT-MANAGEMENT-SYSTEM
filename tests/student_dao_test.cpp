
#include "student_dao.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {

constexpr const char* kDbPath = "build/student_dao_test.db";
constexpr const char* kSchemaPath = "sql/schema.sql";

void removeTestDatabase() {
    std::remove(kDbPath);
    std::remove("build/student_dao_test.db-wal");
    std::remove("build/student_dao_test.db-shm");
}

class StudentDAOTest : public ::testing::Test {
protected:
    srms::Database database_;
    std::unique_ptr<srms::StudentDAO> dao_;

    void SetUp() override {
        removeTestDatabase();

        ASSERT_TRUE(
            database_.open(kDbPath, kSchemaPath)
        );

        dao_ = std::make_unique<srms::StudentDAO>(
            database_
        );
    }

    void TearDown() override {
        dao_.reset();
        database_.close();
        removeTestDatabase();
    }

    srms::Student sampleStudent() {
        srms::Student student{};

        student.roll_no = "PES1UG24CS124";
        student.name = "Test Student";
        student.department = "CSE";
        student.semester = 5;
        student.contact = "";
        student.cgpa = 8.50;
        student.created_at = "2026-10-29 08:00:00";
        student.updated_at = "2026-10-29 08:00:00";

        return student;
    }
};

TEST_F(StudentDAOTest, MissingStudentReturnsFalse) {
    const auto result = dao_->exists("PES1UG24CS124");

    EXPECT_EQ(result.status, srms::Status::OK);
    EXPECT_FALSE(result.value);
}

TEST_F(StudentDAOTest, InsertAndFindStudent) {
    const srms::Student student = sampleStudent();

    EXPECT_EQ(
        dao_->insert(student),
        srms::Status::OK
    );

    const auto result = dao_->exists("pes1ug24cs124");

    EXPECT_EQ(result.status, srms::Status::OK);
    EXPECT_TRUE(result.value);
}

TEST_F(StudentDAOTest, DuplicateRollNumberIsRejected) {
    const srms::Student student = sampleStudent();

    ASSERT_EQ(
        dao_->insert(student),
        srms::Status::OK
    );

    EXPECT_EQ(
        dao_->insert(student),
        srms::Status::DUPLICATE_KEY
    );
}

TEST_F(StudentDAOTest, FindsByDepartmentAndSemester) {
    const srms::Student student = sampleStudent();

    ASSERT_EQ(
        dao_->insert(student),
        srms::Status::OK
    );

    const auto matching =
        dao_->findByDeptSem("CSE", 5);

    ASSERT_EQ(matching.status, srms::Status::OK);
    ASSERT_EQ(matching.value.size(), 1U);

    EXPECT_EQ(
        matching.value[0].roll_no,
        student.roll_no
    );
    EXPECT_EQ(
        matching.value[0].name,
        student.name
    );

    const auto noEce =
        dao_->findByDeptSem("ECE", 5);

    ASSERT_EQ(noEce.status, srms::Status::OK);
    EXPECT_TRUE(noEce.value.empty());

    const auto noSemester =
        dao_->findByDeptSem("CSE", 4);

    ASSERT_EQ(noSemester.status, srms::Status::OK);
    EXPECT_TRUE(noSemester.value.empty());
}

TEST_F(StudentDAOTest, SQLLookingRollNumberIsTreatedAsData) {
    const srms::Student student = sampleStudent();

    ASSERT_EQ(
        dao_->insert(student),
        srms::Status::OK
    );

    const auto result =
        dao_->exists("' OR 1=1 --");

    EXPECT_EQ(result.status, srms::Status::OK);
    EXPECT_FALSE(result.value);

    const auto matching =
        dao_->findByDeptSem("CSE", 5);

    ASSERT_EQ(matching.status, srms::Status::OK);
    ASSERT_EQ(matching.value.size(), 1U);
    EXPECT_EQ(matching.value[0].roll_no, student.roll_no);
}

TEST_F(StudentDAOTest, StudentPersistsAfterCloseAndReopen) {
    const srms::Student student = sampleStudent();

    ASSERT_EQ(dao_->insert(student), srms::Status::OK);

    // Destroy the DAO before closing the database connection.
    dao_.reset();
    database_.close();

    // Reopen the same database file.
    ASSERT_TRUE(database_.open(kDbPath, kSchemaPath));

    dao_ = std::make_unique<srms::StudentDAO>(database_);

    const auto result = dao_->exists(student.roll_no);

    EXPECT_EQ(result.status, srms::Status::OK);
    EXPECT_TRUE(result.value);
}

TEST_F(StudentDAOTest, FailedInsertRollsBack) {
    // Force every student insert to fail inside SQLite.
    const char* trigger_sql =
        "CREATE TRIGGER fail_student_insert "
        "BEFORE INSERT ON students "
        "BEGIN "
        "SELECT RAISE(ABORT, 'forced test failure'); "
        "END;";

    ASSERT_EQ(
        sqlite3_exec(
            database_.handle(),
            trigger_sql,
            nullptr,
            nullptr,
            nullptr
        ),
        SQLITE_OK
    );

    const srms::Student student = sampleStudent();

    EXPECT_EQ(
        dao_->insert(student),
        srms::Status::DB_ERROR
    );

    // The failed insert must not leave a student row behind.
    const auto result = dao_->exists(student.roll_no);

    EXPECT_EQ(result.status, srms::Status::OK);
    EXPECT_FALSE(result.value);
}

}  // namespace

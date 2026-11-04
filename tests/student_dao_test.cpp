#include "student_dao.h"

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace {

constexpr const char* kDbPath = "build/student_dao_test.db";
constexpr const char* kSchemaPath = "sql/schema.sql";

void removeTestDatabase() {
    std::filesystem::create_directories("build");
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

// Existing tests

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

    EXPECT_EQ(matching.value[0].roll_no, student.roll_no);
    EXPECT_EQ(matching.value[0].name, student.name);

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

    dao_.reset();
    database_.close();

    ASSERT_TRUE(database_.open(kDbPath, kSchemaPath));

    dao_ = std::make_unique<srms::StudentDAO>(database_);

    const auto result = dao_->exists(student.roll_no);

    EXPECT_EQ(result.status, srms::Status::OK);
    EXPECT_TRUE(result.value);
}

TEST_F(StudentDAOTest, FailedInsertRollsBack) {
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

    const auto result = dao_->exists(student.roll_no);

    EXPECT_EQ(result.status, srms::Status::OK);
    EXPECT_FALSE(result.value);
}

// New tests: find()

TEST_F(StudentDAOTest, FindReturnsExistingStudent) {
    const auto student = sampleStudent();

    ASSERT_EQ(dao_->insert(student), srms::Status::OK);

    const auto result = dao_->find("pes1ug24cs124");

    ASSERT_EQ(result.status, srms::Status::OK);
    EXPECT_EQ(result.value.roll_no, student.roll_no);
    EXPECT_EQ(result.value.name, student.name);
    EXPECT_EQ(result.value.department, student.department);
    EXPECT_EQ(result.value.semester, student.semester);
    EXPECT_DOUBLE_EQ(result.value.cgpa, student.cgpa);
}

TEST_F(StudentDAOTest, FindMissingStudentReturnsNotFound) {
    const auto result = dao_->find("PES1UG24CS999");

    EXPECT_EQ(result.status, srms::Status::NOT_FOUND);
}

// New tests: update()

TEST_F(StudentDAOTest, UpdateChangesExistingStudent) {
    auto student = sampleStudent();

    ASSERT_EQ(dao_->insert(student), srms::Status::OK);

    student.name = "Updated Student";
    student.department = "ECE";
    student.semester = 6;
    student.contact = "9876543210";
    student.cgpa = 9.25;
    student.updated_at = "2026-11-03 10:00:00";

    EXPECT_EQ(
        dao_->update(student),
        srms::Status::OK
    );

    const auto result = dao_->find(student.roll_no);

    ASSERT_EQ(result.status, srms::Status::OK);
    EXPECT_EQ(result.value.name, "Updated Student");
    EXPECT_EQ(result.value.department, "ECE");
    EXPECT_EQ(result.value.semester, 6);
    EXPECT_EQ(result.value.contact, "9876543210");
    EXPECT_DOUBLE_EQ(result.value.cgpa, 9.25);
}

TEST_F(StudentDAOTest, UpdateMissingStudentReturnsNotFound) {
    const auto student = sampleStudent();

    EXPECT_EQ(
        dao_->update(student),
        srms::Status::NOT_FOUND
    );
}

// New tests: remove()

TEST_F(StudentDAOTest, RemoveDeletesExistingStudent) {
    const auto student = sampleStudent();

    ASSERT_EQ(dao_->insert(student), srms::Status::OK);

    EXPECT_EQ(
        dao_->remove(student.roll_no),
        srms::Status::OK
    );

    const auto result = dao_->find(student.roll_no);

    EXPECT_EQ(result.status, srms::Status::NOT_FOUND);
}

TEST_F(StudentDAOTest, RemoveMissingStudentReturnsNotFound) {
    EXPECT_EQ(
        dao_->remove("PES1UG24CS999"),
        srms::Status::NOT_FOUND
    );
}

// New tests: count()

TEST_F(StudentDAOTest, CountReturnsZeroForEmptyDatabase) {
    const auto result = dao_->count();

    ASSERT_EQ(result.status, srms::Status::OK);
    EXPECT_EQ(result.value, 0);
}

TEST_F(StudentDAOTest, CountReturnsNumberOfStudents) {
    auto first = sampleStudent();
    auto second = sampleStudent();

    second.roll_no = "PES1UG24CS125";

    ASSERT_EQ(dao_->insert(first), srms::Status::OK);
    ASSERT_EQ(dao_->insert(second), srms::Status::OK);

    const auto result = dao_->count();

    ASSERT_EQ(result.status, srms::Status::OK);
    EXPECT_EQ(result.value, 2);
}

// New tests: fetch()

TEST_F(StudentDAOTest, FetchReturnsStudentsInRollNumberOrder) {
    auto first = sampleStudent();
    auto second = sampleStudent();
    auto third = sampleStudent();

    first.roll_no = "PES1UG24CS103";
    second.roll_no = "PES1UG24CS101";
    third.roll_no = "PES1UG24CS102";

    ASSERT_EQ(dao_->insert(first), srms::Status::OK);
    ASSERT_EQ(dao_->insert(second), srms::Status::OK);
    ASSERT_EQ(dao_->insert(third), srms::Status::OK);

    const auto result = dao_->fetch(0, 3);

    ASSERT_EQ(result.status, srms::Status::OK);
    ASSERT_EQ(result.value.size(), 3U);

    EXPECT_EQ(result.value[0].roll_no, "PES1UG24CS101");
    EXPECT_EQ(result.value[1].roll_no, "PES1UG24CS102");
    EXPECT_EQ(result.value[2].roll_no, "PES1UG24CS103");
}

TEST_F(StudentDAOTest, FetchReturnsCorrectPage) {
    auto first = sampleStudent();
    auto second = sampleStudent();
    auto third = sampleStudent();

    first.roll_no = "PES1UG24CS101";
    second.roll_no = "PES1UG24CS102";
    third.roll_no = "PES1UG24CS103";

    ASSERT_EQ(dao_->insert(first), srms::Status::OK);
    ASSERT_EQ(dao_->insert(second), srms::Status::OK);
    ASSERT_EQ(dao_->insert(third), srms::Status::OK);

    const auto result = dao_->fetch(1, 1);

    ASSERT_EQ(result.status, srms::Status::OK);
    ASSERT_EQ(result.value.size(), 1U);

    EXPECT_EQ(result.value[0].roll_no, "PES1UG24CS102");
}

TEST_F(StudentDAOTest, FetchBeyondAvailableRecordsReturnsEmpty) {
    const auto result = dao_->fetch(10, 5);

    ASSERT_EQ(result.status, srms::Status::OK);
    EXPECT_TRUE(result.value.empty());
}

TEST_F(StudentDAOTest, FetchRejectsNegativeOffset) {
    const auto result = dao_->fetch(-1, 5);

    EXPECT_EQ(result.status, srms::Status::VALIDATION_ERROR);
}

TEST_F(StudentDAOTest, FetchRejectsNegativeLimit) {
    const auto result = dao_->fetch(0, -1);

    EXPECT_EQ(result.status, srms::Status::VALIDATION_ERROR);
}

}  // namespace
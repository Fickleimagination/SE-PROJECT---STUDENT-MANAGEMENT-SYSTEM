#include "database.h"
#include "student_dao.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace srms {
namespace {

class StudentDAOSearchTest : public ::testing::Test {
protected:
    static constexpr const char* kDbPath =
        "build/student_dao_search_test.db";

    Database database_;
    std::unique_ptr<StudentDAO> dao_;

    void removeDatabaseFiles() {
        std::remove(kDbPath);
        std::remove("build/student_dao_search_test.db-wal");
        std::remove("build/student_dao_search_test.db-shm");
    }

    void SetUp() override {
        removeDatabaseFiles();

        ASSERT_TRUE(database_.open(
            kDbPath,
            "sql/schema.sql"
        ));

        dao_ = std::make_unique<StudentDAO>(database_);
    }

    void TearDown() override {
        dao_.reset();
        database_.close();
        removeDatabaseFiles();
    }

    void addStudent(
        const std::string& roll,
        const std::string& name
    ) {
        Student student{};

        student.roll_no = roll;
        student.name = name;
        student.department = "CSE";
        student.semester = 3;
        student.contact = "9876543210";
        student.cgpa = 8.5;
        student.created_at = "2026-10-20 10:00:00";
        student.updated_at = "2026-10-20 10:00:00";

        ASSERT_EQ(dao_->insert(student), Status::OK);
    }
};

TEST_F(
    StudentDAOSearchTest,
    ExactRollSearchIgnoresLetterCase
) {
    addStudent("CS12345", "Varun Kumar");

    const auto result = dao_->search("cs12345", false);

    ASSERT_EQ(result.status, Status::OK);
    ASSERT_EQ(result.value.size(), 1U);
    EXPECT_EQ(result.value[0].roll_no, "CS12345");
}

TEST_F(
    StudentDAOSearchTest,
    PartialNameSearchIsCaseInsensitiveAndSorted
) {
    addStudent("CS00003", "Varun Kumar");
    addStudent("CS00001", "Avery Varma");
    addStudent("CS00002", "VARUN Rao");

    const auto result = dao_->search("vAr", true);

    ASSERT_EQ(result.status, Status::OK);
    ASSERT_EQ(result.value.size(), 3U);

    EXPECT_EQ(result.value[0].roll_no, "CS00001");
    EXPECT_EQ(result.value[1].roll_no, "CS00002");
    EXPECT_EQ(result.value[2].roll_no, "CS00003");
}

TEST_F(
    StudentDAOSearchTest,
    DisabledNameSearchDoesNotMatchNames
) {
    addStudent("CS12345", "Varun Kumar");

    const auto result = dao_->search("var", false);

    ASSERT_EQ(result.status, Status::OK);
    EXPECT_TRUE(result.value.empty());
}

TEST_F(
    StudentDAOSearchTest,
    LikeWildcardCharactersAreTreatedLiterally
) {
    addStudent("CS12345", "Varun Kumar");

    const auto percentResult =
        dao_->search("Var%", true);

    ASSERT_EQ(percentResult.status, Status::OK);
    EXPECT_TRUE(percentResult.value.empty());

    const auto underscoreResult =
        dao_->search("Var_", true);

    ASSERT_EQ(underscoreResult.status, Status::OK);
    EXPECT_TRUE(underscoreResult.value.empty());
}

}  // namespace
}  // namespace srms
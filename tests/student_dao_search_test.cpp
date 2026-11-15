#include "database.h"
#include "student_dao.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
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
        std::filesystem::create_directories("build");
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
        const std::string& name,
        const std::string& department = "CSE",
        int semester = 3,
        double cgpa = 8.5
    ) {
        Student student{};

        student.roll_no = roll;
        student.name = name;
        student.department = department;
        student.semester = semester;
        student.contact = "9876543210";
        student.cgpa = cgpa;
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

TEST_F(
    StudentDAOSearchTest,
    SortsAllFieldsInBothDirectionsWithRollNumberTieBreak
) {
    addStudent("CS00003", "Student C", "ECE", 2, 9.0);
    addStudent("CS00001", "Student A", "CSE", 2, 8.0);
    addStudent("CS00002", "Student B", "CSE", 1, 8.0);
    addStudent("CS00004", "Student D", "ECE", 2, 7.0);

    auto expectOrder = [this](
        Field field,
        Order order,
        const std::vector<std::string>& expected
    ) {
        const auto result = dao_->sortRecords(field, order);

        ASSERT_EQ(result.status, Status::OK);
        ASSERT_EQ(result.value.size(), expected.size());

        for (std::size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(result.value[i].roll_no, expected[i]);
        }
    };

    expectOrder(Field::Department, Order::Ascending,
        {"CS00001", "CS00002", "CS00003", "CS00004"});

    expectOrder(Field::Department, Order::Descending,
        {"CS00003", "CS00004", "CS00001", "CS00002"});

    expectOrder(Field::Semester, Order::Ascending,
        {"CS00002", "CS00001", "CS00003", "CS00004"});

    expectOrder(Field::Semester, Order::Descending,
        {"CS00001", "CS00003", "CS00004", "CS00002"});

    expectOrder(Field::Cgpa, Order::Ascending,
        {"CS00004", "CS00001", "CS00002", "CS00003"});

    expectOrder(Field::Cgpa, Order::Descending,
        {"CS00003", "CS00001", "CS00002", "CS00004"});
}

TEST_F(
    StudentDAOSearchTest,
    FiltersByDepartmentSemesterAndBoth
) {
    addStudent("CS00001", "Student A", "CSE", 2, 8.0);
    addStudent("CS00002", "Student B", "CSE", 5, 8.5);
    addStudent("CS00003", "Student C", "ECE", 2, 9.0);

    const auto departmentResult =
        dao_->filterRecords(FilterCriteria{"CSE", std::nullopt});

    ASSERT_EQ(departmentResult.status, Status::OK);
    ASSERT_EQ(departmentResult.value.size(), 2U);
    EXPECT_EQ(departmentResult.value[0].roll_no, "CS00001");
    EXPECT_EQ(departmentResult.value[1].roll_no, "CS00002");

    const auto semesterResult =
        dao_->filterRecords(FilterCriteria{std::nullopt, 2});

    ASSERT_EQ(semesterResult.status, Status::OK);
    ASSERT_EQ(semesterResult.value.size(), 2U);
    EXPECT_EQ(semesterResult.value[0].roll_no, "CS00001");
    EXPECT_EQ(semesterResult.value[1].roll_no, "CS00003");

    const auto combinedResult =
        dao_->filterRecords(FilterCriteria{"CSE", 2});

    ASSERT_EQ(combinedResult.status, Status::OK);
    ASSERT_EQ(combinedResult.value.size(), 1U);
    EXPECT_EQ(combinedResult.value[0].roll_no, "CS00001");
}

TEST_F(
    StudentDAOSearchTest,
    RejectsFilterWithoutCriteria
) {
    const auto result =
        dao_->filterRecords(FilterCriteria{});

    EXPECT_EQ(result.status, Status::VALIDATION_ERROR);
    EXPECT_TRUE(result.value.empty());
}

TEST_F(
    StudentDAOSearchTest,
    RejectsInvalidSortEnums
) {
    const auto fieldResult = dao_->sortRecords(
        static_cast<Field>(999),
        Order::Ascending
    );

    EXPECT_EQ(fieldResult.status, Status::VALIDATION_ERROR);

    const auto orderResult = dao_->sortRecords(
        Field::Cgpa,
        static_cast<Order>(999)
    );

    EXPECT_EQ(orderResult.status, Status::VALIDATION_ERROR);
}

}  // namespace
}  // namespace srms
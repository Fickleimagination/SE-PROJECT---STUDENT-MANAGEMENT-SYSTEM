// Topper list with a tie, run against the real SQLite database (TC-RPT: ties case).
// The unit test in report_service_test.cpp uses a fake source; this one checks that
// StudentDAO + ReportService together return every student tied on the highest CGPA.
#include "database.h"
#include "report_service.h"
#include "student_dao.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

namespace srms {
namespace {

constexpr std::time_t kNow = 1000000;
const Session kAdmin{"admin", true, kNow};
constexpr const char* kDbPath = "build/report_ties_test.db";

class ReportTiesTest : public ::testing::Test {
protected:
    Database database_;
    StudentDAO* dao_ = nullptr;

    void removeFiles() {
        std::filesystem::create_directories("build");
        std::remove(kDbPath);
        std::remove("build/report_ties_test.db-wal");
        std::remove("build/report_ties_test.db-shm");
    }

    void SetUp() override {
        removeFiles();
        ASSERT_TRUE(database_.open(kDbPath, "sql/schema.sql"));
        dao_ = new StudentDAO(database_);
    }

    void TearDown() override {
        delete dao_;
        database_.close();
        removeFiles();
    }

    void addStudent(const std::string& roll, const std::string& name, double cgpa) {
        Student s{};
        s.roll_no = roll;
        s.name = name;
        s.department = "CSE";
        s.semester = 5;
        s.contact = "9876543210";
        s.cgpa = cgpa;
        s.created_at = "2026-11-01 10:00:00";
        s.updated_at = "2026-11-01 10:00:00";
        ASSERT_EQ(dao_->insert(s), Status::OK);
    }
};

TEST_F(ReportTiesTest, TwoStudentsTiedOnHighestCgpaAreBothListed) {
    addStudent("PES1UG24CS301", "Asha Rao", 9.50);
    addStudent("PES1UG24CS302", "Bilal Khan", 9.50);
    addStudent("PES1UG24CS303", "Chitra Iyer", 9.10);

    ReportService service(*dao_, nullptr);
    auto result = service.generate(kAdmin, ReportType::TopperList, {"CSE", 5}, kNow);

    ASSERT_EQ(result.status, Status::OK);
    ASSERT_EQ(result.value.rows.size(), 2U);
    EXPECT_EQ(result.value.rows[0].roll_no, "PES1UG24CS301");
    EXPECT_EQ(result.value.rows[1].roll_no, "PES1UG24CS302");
    EXPECT_NEAR(result.value.highest, 9.50, 0.001);
}

TEST_F(ReportTiesTest, SingleTopperIsNotPaddedWithOthers) {
    addStudent("PES1UG24CS301", "Asha Rao", 9.60);
    addStudent("PES1UG24CS302", "Bilal Khan", 9.50);

    ReportService service(*dao_, nullptr);
    auto result = service.generate(kAdmin, ReportType::TopperList, {"CSE", 5}, kNow);

    ASSERT_EQ(result.status, Status::OK);
    ASSERT_EQ(result.value.rows.size(), 1U);
    EXPECT_EQ(result.value.rows[0].roll_no, "PES1UG24CS301");
}

}  // namespace
}  // namespace srms

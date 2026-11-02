// GoogleTest suite for ReportService.
// Covers report generation, export, authorization, and database errors.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../src/core/report_service.h"

using namespace srms;

namespace {

constexpr std::time_t kNow = 1000000;
const Session kAdmin{"admin", true, kNow};

Student mk(
    const std::string& roll,
    const std::string& dept,
    int sem,
    double cgpa
) {
    return Student{
        roll,
        "Name " + roll,
        dept,
        sem,
        "",
        cgpa,
        "2026-10-01 10:00:00",
        "2026-10-01 10:00:00"
    };
}

class FakeSource : public StudentSource {
public:
    std::vector<Student> rows;

    // Allows tests to simulate a failed database read.
    Status resultStatus = Status::OK;
    std::string resultDetail;

    Result<std::vector<Student>> findByDeptSem(
        const std::string& department,
        int semester
    ) override {
        if (resultStatus != Status::OK) {
            return {
                resultStatus,
                {},
                resultDetail
            };
        }

        std::vector<Student> out;

        for (const auto& student : rows) {
            if (student.department == department &&
                student.semester == semester) {
                out.push_back(student);
            }
        }

        return {
            Status::OK,
            std::move(out),
            ""
        };
    }
};

std::string readFile(const std::string& path) {
    std::ifstream input(path);
    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

}  // namespace

class ReportServiceTest : public ::testing::Test {
protected:
    FakeSource source;
    int audits = 0;

    ReportService service{
        source,
        [this](const std::string&, const std::string&) {
            ++audits;
        }
    };

    std::filesystem::path tmp;

    void SetUp() override {
        tmp = std::filesystem::temp_directory_path()
            / "srms_report_tests";

        std::filesystem::remove_all(tmp);

        source.rows = {
            mk("PES1UG24CS201", "CSE", 5, 9.80),
            mk("PES1UG24CS202", "CSE", 5, 9.80),
            mk("PES1UG24CS120", "CSE", 5, 7.20),
            mk("PES1UG24CS150", "CSE", 5, 8.50),
            mk("PES1UG24CS110", "ECE", 5, 9.90)
        };

        source.resultStatus = Status::OK;
        source.resultDetail.clear();
    }

    void TearDown() override {
        std::filesystem::remove_all(tmp);
    }
};

TEST_F(ReportServiceTest, NonAdminIsDenied) {
    Session viewer{"viewer", false, kNow};

    auto result = service.generate(
        viewer, ReportType::ClassList, {"CSE", 5}, kNow
    );

    EXPECT_EQ(result.status, Status::ACCESS_DENIED);
}

TEST_F(ReportServiceTest, IdleSessionIsDenied) {
    Session stale{"admin", true, kNow - 601};

    auto result = service.generate(
        stale, ReportType::ClassList, {"CSE", 5}, kNow
    );

    EXPECT_EQ(result.status, Status::ACCESS_DENIED);
}

TEST_F(ReportServiceTest, InvalidDepartmentIsRejected) {
    auto result = service.generate(
        kAdmin, ReportType::ClassList, {"XYZ", 5}, kNow
    );

    EXPECT_EQ(result.status, Status::VALIDATION_ERROR);
}

TEST_F(ReportServiceTest, SemesterOutOfRangeIsRejected) {
    EXPECT_EQ(
        service.generate(
            kAdmin, ReportType::ClassList, {"CSE", 0}, kNow
        ).status,
        Status::VALIDATION_ERROR
    );

    EXPECT_EQ(
        service.generate(
            kAdmin, ReportType::ClassList, {"CSE", 9}, kNow
        ).status,
        Status::VALIDATION_ERROR
    );
}

TEST_F(ReportServiceTest, ClassListOrderedByRollNumber) {
    auto result = service.generate(
        kAdmin, ReportType::ClassList, {"CSE", 5}, kNow
    );

    ASSERT_EQ(result.status, Status::OK);
    ASSERT_EQ(result.value.rows.size(), 4U);

    EXPECT_EQ(result.value.rows[0].roll_no, "PES1UG24CS120");
    EXPECT_EQ(result.value.rows[3].roll_no, "PES1UG24CS202");
}

TEST_F(ReportServiceTest, AverageRoundedToTwoDecimals) {
    source.rows = {
        mk("A1", "CSE", 5, 7.20),
        mk("A2", "CSE", 5, 8.50),
        mk("A3", "CSE", 5, 9.10)
    };

    auto result = service.generate(
        kAdmin, ReportType::AverageCgpa, {"CSE", 5}, kNow
    );

    ASSERT_EQ(result.status, Status::OK);
    EXPECT_NEAR(result.value.average, 8.27, 0.005);
    EXPECT_EQ(result.value.student_count, 3);
}

TEST_F(ReportServiceTest, EmptySelectionShowsMessage) {
    auto result = service.generate(
        kAdmin, ReportType::AverageCgpa, {"CSE", 2}, kNow
    );

    EXPECT_EQ(result.status, Status::OK);
    EXPECT_EQ(result.value.student_count, 0);
    EXPECT_EQ(
        result.detail,
        "No records available for this report."
    );
}

TEST_F(ReportServiceTest, TopperListIncludesAllTies) {
    auto result = service.generate(
        kAdmin, ReportType::TopperList, {"CSE", 5}, kNow
    );

    ASSERT_EQ(result.status, Status::OK);
    ASSERT_EQ(result.value.rows.size(), 2U);

    EXPECT_EQ(result.value.rows[0].roll_no, "PES1UG24CS201");
    EXPECT_EQ(result.value.rows[1].roll_no, "PES1UG24CS202");
    EXPECT_NEAR(result.value.highest, 9.80, 0.001);
}

TEST_F(ReportServiceTest, DatabaseErrorIsPropagated) {
    source.resultStatus = Status::DB_ERROR;
    source.resultDetail = "database query failed";

    auto result = service.generate(
        kAdmin, ReportType::ClassList, {"CSE", 5}, kNow
    );

    EXPECT_EQ(result.status, Status::DB_ERROR);
    EXPECT_EQ(result.detail, "database query failed");
}

TEST_F(ReportServiceTest, ExportWritesCsvAndAudits) {
    auto generated = service.generate(
        kAdmin, ReportType::TopperList, {"CSE", 5}, kNow
    );

    ASSERT_EQ(generated.status, Status::OK);

    auto exported = service.exportToFile(
        kAdmin, generated.value, Format::CSV, kNow, tmp.string()
    );

    ASSERT_EQ(exported.status, Status::OK);
    EXPECT_EQ(audits, 1);
    EXPECT_TRUE(std::filesystem::exists(exported.value));
    EXPECT_NE(
        exported.value.find("topper_list_"),
        std::string::npos
    );
    EXPECT_EQ(
        exported.value.substr(exported.value.size() - 4),
        ".csv"
    );

    const std::string contents = readFile(exported.value);

    EXPECT_NE(
        contents.find("roll_no,name,department,semester,cgpa"),
        std::string::npos
    );
    EXPECT_NE(
        contents.find("PES1UG24CS201"),
        std::string::npos
    );
}

TEST_F(ReportServiceTest, ExportWritesTabSeparatedTxt) {
    auto generated = service.generate(
        kAdmin, ReportType::ClassList, {"CSE", 5}, kNow
    );

    ASSERT_EQ(generated.status, Status::OK);

    auto exported = service.exportToFile(
        kAdmin, generated.value, Format::TXT, kNow, tmp.string()
    );

    ASSERT_EQ(exported.status, Status::OK);

    const std::string contents = readFile(exported.value);

    EXPECT_NE(
        contents.find("roll_no\tname\tdepartment\tsemester\tcgpa"),
        std::string::npos
    );
    EXPECT_EQ(
        exported.value.substr(exported.value.size() - 4),
        ".txt"
    );
}

TEST_F(ReportServiceTest, EmptyReportCreatesNoFile) {
    auto generated = service.generate(
        kAdmin, ReportType::ClassList, {"BT", 1}, kNow
    );

    ASSERT_EQ(generated.status, Status::OK);

    auto exported = service.exportToFile(
        kAdmin, generated.value, Format::TXT, kNow, tmp.string()
    );

    EXPECT_EQ(exported.status, Status::NOT_FOUND);
    EXPECT_FALSE(std::filesystem::exists(tmp));
    EXPECT_EQ(audits, 0);
}

TEST_F(ReportServiceTest, ExportDeniedWithoutSession) {
    auto generated = service.generate(
        kAdmin, ReportType::ClassList, {"CSE", 5}, kNow
    );

    ASSERT_EQ(generated.status, Status::OK);

    Session viewer{"viewer", false, kNow};

    auto exported = service.exportToFile(
        viewer, generated.value, Format::CSV, kNow, tmp.string()
    );

    EXPECT_EQ(exported.status, Status::ACCESS_DENIED);
}

TEST_F(ReportServiceTest, CsvQuotesFieldsWithCommas) {
    source.rows = {
        Student{
            "A1", "Doe, Jane", "CSE", 5,
            "", 8.00, "", ""
        }
    };

    auto generated = service.generate(
        kAdmin, ReportType::ClassList, {"CSE", 5}, kNow
    );

    ASSERT_EQ(generated.status, Status::OK);

    auto exported = service.exportToFile(
        kAdmin, generated.value, Format::CSV, kNow, tmp.string()
    );

    ASSERT_EQ(exported.status, Status::OK);

    EXPECT_NE(
        readFile(exported.value).find("\"Doe, Jane\""),
        std::string::npos
    );
}

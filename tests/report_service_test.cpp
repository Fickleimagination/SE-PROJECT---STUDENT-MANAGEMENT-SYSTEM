// GoogleTest suite for ReportService (SAD 4.3.2; test plan TC-07, TC-08, TC-RPT-01..03).
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "../src/core/report_service.h"

using namespace srms;

namespace {

constexpr std::time_t kNow = 1000000;
const Session kAdmin{"admin", true, kNow};

Student mk(const std::string& roll, const std::string& dept, int sem, double cgpa) {
    return Student{roll, "Name " + roll, dept, sem, "", cgpa, "2026-10-01 10:00:00", "2026-10-01 10:00:00"};
}

class FakeSource : public StudentSource {
public:
    std::vector<Student> rows;
    std::vector<Student> findByDeptSem(const std::string& d, int s) override {
        std::vector<Student> out;
        for (const auto& r : rows)
            if (r.department == d && r.semester == s) out.push_back(r);
        return out;
    }
};

std::string readFile(const std::string& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

class ReportServiceTest : public ::testing::Test {
protected:
    FakeSource source;
    int audits = 0;
    ReportService service{source, [this](const std::string&, const std::string&) { ++audits; }};
    std::filesystem::path tmp;

    void SetUp() override {
        tmp = std::filesystem::temp_directory_path() / "srms_report_tests";
        std::filesystem::remove_all(tmp);
        source.rows = {
            mk("PES1UG24CS201", "CSE", 5, 9.80),
            mk("PES1UG24CS202", "CSE", 5, 9.80),
            mk("PES1UG24CS120", "CSE", 5, 7.20),
            mk("PES1UG24CS150", "CSE", 5, 8.50),
            mk("PES1UG24CS110", "ECE", 5, 9.90),
        };
    }
    void TearDown() override { std::filesystem::remove_all(tmp); }
};

TEST_F(ReportServiceTest, NonAdminIsDenied) {
    Session viewer{"viewer", false, kNow};
    auto r = service.generate(viewer, ReportType::ClassList, {"CSE", 5}, kNow);
    EXPECT_EQ(r.status, Status::ACCESS_DENIED);  // REQ-AUTH-06 / SEC-07
}

TEST_F(ReportServiceTest, IdleSessionIsDenied) {
    Session stale{"admin", true, kNow - 601};    // REQ-AUTH-08: 10 minutes
    auto r = service.generate(stale, ReportType::ClassList, {"CSE", 5}, kNow);
    EXPECT_EQ(r.status, Status::ACCESS_DENIED);
}

TEST_F(ReportServiceTest, InvalidDepartmentIsRejected) {
    auto r = service.generate(kAdmin, ReportType::ClassList, {"XYZ", 5}, kNow);
    EXPECT_EQ(r.status, Status::VALIDATION_ERROR);  // REQ-DATA-05
}

TEST_F(ReportServiceTest, SemesterOutOfRangeIsRejected) {
    EXPECT_EQ(service.generate(kAdmin, ReportType::ClassList, {"CSE", 0}, kNow).status,
              Status::VALIDATION_ERROR);
    EXPECT_EQ(service.generate(kAdmin, ReportType::ClassList, {"CSE", 9}, kNow).status,
              Status::VALIDATION_ERROR);  // REQ-DATA-04
}

TEST_F(ReportServiceTest, ClassListOrderedByRollNumber) {  // REQ-RPT-01
    auto r = service.generate(kAdmin, ReportType::ClassList, {"CSE", 5}, kNow);
    ASSERT_EQ(r.status, Status::OK);
    ASSERT_EQ(r.value.rows.size(), 4u);
    EXPECT_EQ(r.value.rows[0].roll_no, "PES1UG24CS120");
    EXPECT_EQ(r.value.rows[3].roll_no, "PES1UG24CS202");
}

TEST_F(ReportServiceTest, AverageRoundedToTwoDecimals) {  // REQ-RPT-02, TC-07
    source.rows = {mk("A1", "CSE", 5, 7.20), mk("A2", "CSE", 5, 8.50), mk("A3", "CSE", 5, 9.10)};
    auto r = service.generate(kAdmin, ReportType::AverageCgpa, {"CSE", 5}, kNow);
    ASSERT_EQ(r.status, Status::OK);
    EXPECT_NEAR(r.value.average, 8.27, 0.005);
    EXPECT_EQ(r.value.student_count, 3);
}

TEST_F(ReportServiceTest, EmptySelectionShowsMessage) {  // REQ-RPT-05
    auto r = service.generate(kAdmin, ReportType::AverageCgpa, {"CSE", 2}, kNow);
    EXPECT_EQ(r.status, Status::OK);
    EXPECT_EQ(r.value.student_count, 0);
    EXPECT_EQ(r.detail, "No records available for this report.");
}

TEST_F(ReportServiceTest, TopperListIncludesAllTies) {  // REQ-RPT-03, TC-08
    auto r = service.generate(kAdmin, ReportType::TopperList, {"CSE", 5}, kNow);
    ASSERT_EQ(r.status, Status::OK);
    ASSERT_EQ(r.value.rows.size(), 2u);
    EXPECT_EQ(r.value.rows[0].roll_no, "PES1UG24CS201");
    EXPECT_EQ(r.value.rows[1].roll_no, "PES1UG24CS202");
    EXPECT_NEAR(r.value.highest, 9.80, 0.001);
}

TEST_F(ReportServiceTest, ExportWritesCsvAndAudits) {  // REQ-RPT-04, SEC-04
    auto gen = service.generate(kAdmin, ReportType::TopperList, {"CSE", 5}, kNow);
    auto ex = service.exportToFile(kAdmin, gen.value, Format::CSV, kNow, tmp.string());
    ASSERT_EQ(ex.status, Status::OK);
    EXPECT_EQ(audits, 1);
    EXPECT_TRUE(std::filesystem::exists(ex.value));
    EXPECT_NE(ex.value.find("topper_list_"), std::string::npos);
    EXPECT_EQ(ex.value.substr(ex.value.size() - 4), ".csv");
    std::string text = readFile(ex.value);
    EXPECT_NE(text.find("roll_no,name,department,semester,cgpa"), std::string::npos);
    EXPECT_NE(text.find("PES1UG24CS201"), std::string::npos);
}

TEST_F(ReportServiceTest, ExportWritesTabSeparatedTxt) {  // REQ-RPT-04
    auto gen = service.generate(kAdmin, ReportType::ClassList, {"CSE", 5}, kNow);
    auto ex = service.exportToFile(kAdmin, gen.value, Format::TXT, kNow, tmp.string());
    ASSERT_EQ(ex.status, Status::OK);
    std::string text = readFile(ex.value);
    EXPECT_NE(text.find("roll_no\tname\tdepartment\tsemester\tcgpa"), std::string::npos);
    EXPECT_EQ(ex.value.substr(ex.value.size() - 4), ".txt");
}

TEST_F(ReportServiceTest, EmptyReportCreatesNoFile) {  // REQ-RPT-05
    auto gen = service.generate(kAdmin, ReportType::ClassList, {"BT", 1}, kNow);
    auto ex = service.exportToFile(kAdmin, gen.value, Format::TXT, kNow, tmp.string());
    EXPECT_EQ(ex.status, Status::NOT_FOUND);
    EXPECT_FALSE(std::filesystem::exists(tmp));
    EXPECT_EQ(audits, 0);
}

TEST_F(ReportServiceTest, ExportDeniedWithoutSession) {  // REQ-AUTH-06
    auto gen = service.generate(kAdmin, ReportType::ClassList, {"CSE", 5}, kNow);
    Session viewer{"viewer", false, kNow};
    auto ex = service.exportToFile(viewer, gen.value, Format::CSV, kNow, tmp.string());
    EXPECT_EQ(ex.status, Status::ACCESS_DENIED);
}

TEST_F(ReportServiceTest, FutureSessionTimestampIsDenied) {
    Session future{"admin", true, kNow + 3600};
    auto r = service.generate(future, ReportType::ClassList, {"CSE", 5}, kNow);
    EXPECT_EQ(r.status, Status::ACCESS_DENIED);
}

TEST_F(ReportServiceTest, CsvQuotesFieldsWithCommas) {
    source.rows = {Student{"A1", "Doe, Jane", "CSE", 5, "", 8.00, "", ""}};
    auto gen = service.generate(kAdmin, ReportType::ClassList, {"CSE", 5}, kNow);
    auto ex = service.exportToFile(kAdmin, gen.value, Format::CSV, kNow, tmp.string());
    ASSERT_EQ(ex.status, Status::OK);
    EXPECT_NE(readFile(ex.value).find("\"Doe, Jane\""), std::string::npos);
}

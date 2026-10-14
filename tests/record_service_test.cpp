// GoogleTest suite for RecordService::addStudent (SAD 4.2.2 and 4.3.2;
// test plan TC-CRUD-01, TC-CRUD-02, TC-DATA-02, TC-SEC-07).
#include <gtest/gtest.h>

#include <map>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/record_service.h"

using namespace srms;

namespace {

constexpr std::time_t kNow = 1000000;
const Session kAdmin{"admin", true, kNow};

Student goodStudent() {
    return Student{"PES1UG24CS116", "Bhavyaa Garg", "CSE", 5, "9876543210", 8.41, "", ""};
}

// In-memory stand-in for StudentDAO.
class FakeRepo : public StudentRepository {
public:
    std::map<std::string, Student> rows;
    Status existsFails = Status::OK;   // set to DB_ERROR to simulate a failed SELECT
    Status insertResult = Status::OK;  // set to DB_ERROR / DUPLICATE_KEY to simulate a failed INSERT
    bool throwOnInsert = false;
    int insertCalls = 0;

    Result<bool> exists(const std::string& roll_no) override {
        if (existsFails != Status::OK) return {existsFails, false, ""};
        return {Status::OK, rows.count(roll_no) > 0, ""};
    }
    Status insert(const Student& s) override {
        ++insertCalls;
        if (throwOnInsert) throw std::runtime_error("boom");
        if (insertResult != Status::OK) return insertResult;
        rows[s.roll_no] = s;
        return Status::OK;
    }
};

struct AuditRow {
    std::string event, username, roll_no, outcome;
};

}  // namespace

class RecordServiceAddTest : public ::testing::Test {
protected:
    FakeRepo repo;
    std::vector<AuditRow> audits;
    RecordService service{repo,
                          [this](const std::string& e, const std::string& u, const std::string& r,
                                 const std::string& o) { audits.push_back({e, u, r, o}); },
                          Validator(), [] { return kNow; }};
};

// TC-CRUD-01 / REQ-CRUD-01: the stored record holds all six entered values.
TEST_F(RecordServiceAddTest, AddsValidRecordWithAllSixFields) {
    EXPECT_EQ(service.addStudent(kAdmin, goodStudent()), Status::OK);
    ASSERT_EQ(repo.rows.size(), 1u);
    const Student& s = repo.rows.at("PES1UG24CS116");
    EXPECT_EQ(s.name, "Bhavyaa Garg");
    EXPECT_EQ(s.department, "CSE");
    EXPECT_EQ(s.semester, 5);
    EXPECT_EQ(s.contact, "9876543210");
    EXPECT_DOUBLE_EQ(s.cgpa, 8.41);
}

TEST_F(RecordServiceAddTest, ContactIsOptional) {
    Student s = goodStudent();
    s.contact = "";
    EXPECT_EQ(service.addStudent(kAdmin, s), Status::OK);
}

TEST_F(RecordServiceAddTest, StoresRollNoUpperCaseAndNameTrimmed) {
    Student s = goodStudent();
    s.roll_no = "pes1ug24cs116";
    s.name = "  Bhavyaa Garg ";
    EXPECT_EQ(service.addStudent(kAdmin, s), Status::OK);
    ASSERT_EQ(repo.rows.count("PES1UG24CS116"), 1u);
    EXPECT_EQ(repo.rows.at("PES1UG24CS116").name, "Bhavyaa Garg");
}

// REQ-CRUD-08: both timestamps set by the system, format YYYY-MM-DD HH:MM:SS.
TEST_F(RecordServiceAddTest, SetsBothTimestampsInRequiredFormat) {
    Student s = goodStudent();
    s.created_at = "1999-01-01 00:00:00";  // caller-supplied values must be ignored
    s.updated_at = "1999-01-01 00:00:00";
    ASSERT_EQ(service.addStudent(kAdmin, s), Status::OK);
    const Student& stored = repo.rows.at("PES1UG24CS116");
    std::regex format(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$)");
    EXPECT_TRUE(std::regex_match(stored.created_at, format));
    EXPECT_EQ(stored.created_at, stored.updated_at);
    EXPECT_NE(stored.created_at, "1999-01-01 00:00:00");
}

// TC-CRUD-02 / REQ-CRUD-02: duplicate rejected, count unchanged.
TEST_F(RecordServiceAddTest, RejectsDuplicateRollNoAndKeepsCountUnchanged) {
    ASSERT_EQ(service.addStudent(kAdmin, goodStudent()), Status::OK);
    Student again = goodStudent();
    again.name = "Someone Else";
    EXPECT_EQ(service.addStudent(kAdmin, again), Status::DUPLICATE_KEY);
    EXPECT_EQ(repo.rows.size(), 1u);
    EXPECT_EQ(repo.rows.at("PES1UG24CS116").name, "Bhavyaa Garg");  // original untouched
}

TEST_F(RecordServiceAddTest, DuplicateIsDetectedIgnoringRollNoCase) {
    ASSERT_EQ(service.addStudent(kAdmin, goodStudent()), Status::OK);
    Student lower = goodStudent();
    lower.roll_no = "pes1ug24cs116";
    EXPECT_EQ(service.addStudent(kAdmin, lower), Status::DUPLICATE_KEY);
}

// If another writer slips in between exists() and insert(), the primary key still protects us.
TEST_F(RecordServiceAddTest, PassesThroughDuplicateKeyFromRepository) {
    repo.insertResult = Status::DUPLICATE_KEY;
    EXPECT_EQ(service.addStudent(kAdmin, goodStudent()), Status::DUPLICATE_KEY);
    EXPECT_TRUE(audits.empty());
}

// SEC-07 / REQ-AUTH-06: no active administrator session, no write.
TEST_F(RecordServiceAddTest, ViewerSessionIsDenied) {
    Session viewer{"viewer", false, kNow};
    EXPECT_EQ(service.addStudent(viewer, goodStudent()), Status::ACCESS_DENIED);
    EXPECT_EQ(repo.insertCalls, 0);
    EXPECT_TRUE(repo.rows.empty());
}

TEST_F(RecordServiceAddTest, IdleSessionIsDenied) {
    Session stale{"admin", true, kNow - 601};  // idle for more than 10 minutes (REQ-AUTH-08)
    EXPECT_EQ(service.addStudent(stale, goodStudent()), Status::ACCESS_DENIED);
    Session justInTime{"admin", true, kNow - 600};
    EXPECT_EQ(service.addStudent(justInTime, goodStudent()), Status::OK);
}

// TC-DATA-02: every Appendix B rule is re-checked inside the service.
TEST_F(RecordServiceAddTest, RejectsEachInvalidFieldAndSavesNothing) {
    std::vector<Student> bad(6, goodStudent());
    bad[0].roll_no = "AB1";
    bad[1].name = "R2D2";
    bad[2].department = "XYZ";
    bad[3].semester = 9;
    bad[4].contact = "12345";
    bad[5].cgpa = 10.01;
    for (const Student& s : bad)
        EXPECT_EQ(service.addStudent(kAdmin, s), Status::VALIDATION_ERROR) << s.roll_no << " " << s.name;
    EXPECT_EQ(repo.insertCalls, 0);
    EXPECT_TRUE(audits.empty());
}

// REQ-DATA-07 / SAD 4.4.4: a storage failure is reported, nothing is audited.
TEST_F(RecordServiceAddTest, ReportsDbErrorWhenInsertFails) {
    repo.insertResult = Status::DB_ERROR;
    EXPECT_EQ(service.addStudent(kAdmin, goodStudent()), Status::DB_ERROR);
    EXPECT_TRUE(repo.rows.empty());
    EXPECT_TRUE(audits.empty());
}

TEST_F(RecordServiceAddTest, ReportsDbErrorWhenExistsCheckFails) {
    repo.existsFails = Status::DB_ERROR;
    EXPECT_EQ(service.addStudent(kAdmin, goodStudent()), Status::DB_ERROR);
    EXPECT_EQ(repo.insertCalls, 0);
}

TEST_F(RecordServiceAddTest, ConvertsUnexpectedExceptionToInternalError) {
    repo.throwOnInsert = true;
    EXPECT_EQ(service.addStudent(kAdmin, goodStudent()), Status::INTERNAL_ERROR);
}

// SEC-04: one RECORD_ADDED row per successful add, none otherwise.
TEST_F(RecordServiceAddTest, AuditsExactlyOneRecordAddedEventOnSuccess) {
    ASSERT_EQ(service.addStudent(kAdmin, goodStudent()), Status::OK);
    ASSERT_EQ(audits.size(), 1u);
    EXPECT_EQ(audits[0].event, "RECORD_ADDED");
    EXPECT_EQ(audits[0].username, "admin");
    EXPECT_EQ(audits[0].roll_no, "PES1UG24CS116");
    EXPECT_EQ(audits[0].outcome, "SUCCESS");
}

TEST_F(RecordServiceAddTest, NoAuditRowForRejectedAdds) {
    ASSERT_EQ(service.addStudent(kAdmin, goodStudent()), Status::OK);
    service.addStudent(kAdmin, goodStudent());              // duplicate
    service.addStudent(Session{"v", false, kNow}, goodStudent());  // denied
    EXPECT_EQ(audits.size(), 1u);
}

// Defaults: no audit hook and the real clock must both work.
TEST(RecordServiceDefaults, WorksWithoutAuditHookAndWithSystemClock) {
    FakeRepo repo;
    RecordService service{repo};
    Session session{"admin", true, std::time(nullptr)};
    EXPECT_EQ(service.addStudent(session, goodStudent()), Status::OK);
}

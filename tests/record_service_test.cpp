// GoogleTest suite for RecordService: add, update, delete, list (SAD 4.2.2, 4.3.2;
// test plan TC-CRUD-01..05, TC-DATA-02, TC-SEC-07).
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
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
    Status findFails = Status::OK;    // DB_ERROR to simulate a failed SELECT in find()
    Status updateResult = Status::OK;
    Status removeResult = Status::OK;
    Status countFails = Status::OK;
    Status fetchFails = Status::OK;
    int updateCalls = 0;
    int removeCalls = 0;

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
    Result<Student> find(const std::string& roll_no) override {
        if (findFails != Status::OK) return {findFails, Student{}, ""};
        auto it = rows.find(roll_no);
        if (it == rows.end()) return {Status::NOT_FOUND, Student{}, ""};
        return {Status::OK, it->second, ""};
    }
    Status update(const Student& s) override {
        ++updateCalls;
        if (updateResult != Status::OK) return updateResult;
        if (!rows.count(s.roll_no)) return Status::NOT_FOUND;
        rows[s.roll_no] = s;
        return Status::OK;
    }
    Status remove(const std::string& roll_no) override {
        ++removeCalls;
        if (removeResult != Status::OK) return removeResult;
        return rows.erase(roll_no) ? Status::OK : Status::NOT_FOUND;
    }
    Result<int> count() override {
        if (countFails != Status::OK) return {countFails, 0, ""};
        return {Status::OK, static_cast<int>(rows.size()), ""};
    }
    Result<std::vector<Student>> fetch(int offset, int limit) override {
        if (fetchFails != Status::OK) return {fetchFails, {}, ""};
        std::vector<Student> out;  // std::map iterates in roll_no order, like ORDER BY roll_no
        int i = 0;
        for (const auto& kv : rows) {
            if (i++ < offset) continue;
            if (static_cast<int>(out.size()) == limit) break;
            out.push_back(kv.second);
        }
        return {Status::OK, out, ""};
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

// ---------------------------------------------------------------------------------------------
// US-04 Update (UC-04; REQ-CRUD-03, 04, 07; SEC-04, SEC-07)
// ---------------------------------------------------------------------------------------------
class RecordServiceUpdateTest : public RecordServiceAddTest {
protected:
    void SetUp() override { ASSERT_EQ(service.addStudent(kAdmin, goodStudent()), Status::OK); audits.clear(); }
};

// REQ-CRUD-03: each of the five editable fields can be changed.
TEST_F(RecordServiceUpdateTest, ChangesEachEditableField) {
    StudentPatch patch;
    patch.name = "Bhavyaa G";
    patch.department = "ECE";
    patch.contact = "bg@pes.edu";
    patch.semester = 6;
    patch.cgpa = 9.1;
    ASSERT_EQ(service.updateStudent(kAdmin, "PES1UG24CS116", patch), Status::OK);
    const Student& s = repo.rows.at("PES1UG24CS116");
    EXPECT_EQ(s.name, "Bhavyaa G");
    EXPECT_EQ(s.department, "ECE");
    EXPECT_EQ(s.contact, "bg@pes.edu");
    EXPECT_EQ(s.semester, 6);
    EXPECT_DOUBLE_EQ(s.cgpa, 9.1);
}

// REQ-CRUD-04: a field left out of the patch keeps its value.
TEST_F(RecordServiceUpdateTest, KeepsFieldsNotInThePatch) {
    StudentPatch patch;
    patch.cgpa = 9.0;
    ASSERT_EQ(service.updateStudent(kAdmin, "PES1UG24CS116", patch), Status::OK);
    const Student& s = repo.rows.at("PES1UG24CS116");
    EXPECT_EQ(s.name, "Bhavyaa Garg");
    EXPECT_EQ(s.department, "CSE");
    EXPECT_EQ(s.semester, 5);
    EXPECT_EQ(s.contact, "9876543210");
}

// REQ-CRUD-04: Enter on every prompt = empty patch; only updated_at moves.
TEST_F(RecordServiceUpdateTest, EmptyPatchChangesOnlyUpdatedAt) {
    Student before = repo.rows.at("PES1UG24CS116");
    RecordService later{repo, nullptr, Validator(), [] { return kNow + 3600; }};
    Session session{"admin", true, kNow + 3600};
    ASSERT_EQ(later.updateStudent(session, "PES1UG24CS116", StudentPatch{}), Status::OK);
    const Student& after = repo.rows.at("PES1UG24CS116");
    EXPECT_EQ(after.name, before.name);
    EXPECT_EQ(after.cgpa, before.cgpa);
    EXPECT_EQ(after.created_at, before.created_at);
    EXPECT_NE(after.updated_at, before.updated_at);
}

TEST_F(RecordServiceUpdateTest, RollNoIsFoundIgnoringCaseAndSpaces) {
    StudentPatch patch;
    patch.semester = 7;
    EXPECT_EQ(service.updateStudent(kAdmin, "  pes1ug24cs116 ", patch), Status::OK);
    EXPECT_EQ(repo.rows.at("PES1UG24CS116").semester, 7);
}

// REQ-CRUD-07: unknown roll_no -> NOT_FOUND and nothing changes.
TEST_F(RecordServiceUpdateTest, UnknownRollNoIsNotFoundAndChangesNothing) {
    StudentPatch patch;
    patch.name = "Nobody";
    EXPECT_EQ(service.updateStudent(kAdmin, "PES1UG24CS999", patch), Status::NOT_FOUND);
    EXPECT_EQ(repo.updateCalls, 0);
    EXPECT_EQ(repo.rows.size(), 1u);
    EXPECT_TRUE(audits.empty());
}

// TC-DATA-02: the merged record is validated; a bad value is rejected and nothing is saved.
TEST_F(RecordServiceUpdateTest, RejectsInvalidValuesAndKeepsStoredRecord) {
    Student before = repo.rows.at("PES1UG24CS116");
    StudentPatch bad1; bad1.cgpa = 10.5;
    StudentPatch bad2; bad2.semester = 0;
    StudentPatch bad3; bad3.department = "XYZ";
    StudentPatch bad4; bad4.name = "R2D2";
    StudentPatch bad5; bad5.contact = "12345";
    for (const StudentPatch& p : {bad1, bad2, bad3, bad4, bad5})
        EXPECT_EQ(service.updateStudent(kAdmin, "PES1UG24CS116", p), Status::VALIDATION_ERROR);
    EXPECT_EQ(repo.updateCalls, 0);
    EXPECT_EQ(repo.rows.at("PES1UG24CS116").name, before.name);
    EXPECT_TRUE(audits.empty());
}

TEST_F(RecordServiceUpdateTest, ClearingContactIsAllowed) {
    StudentPatch patch;
    patch.contact = "";
    EXPECT_EQ(service.updateStudent(kAdmin, "PES1UG24CS116", patch), Status::OK);
    EXPECT_EQ(repo.rows.at("PES1UG24CS116").contact, "");
}

// SEC-07: only an active administrator may update.
TEST_F(RecordServiceUpdateTest, ViewerAndIdleSessionsAreDenied) {
    StudentPatch patch;
    patch.name = "X";
    EXPECT_EQ(service.updateStudent(Session{"v", false, kNow}, "PES1UG24CS116", patch), Status::ACCESS_DENIED);
    EXPECT_EQ(service.updateStudent(Session{"admin", true, kNow - 601}, "PES1UG24CS116", patch),
              Status::ACCESS_DENIED);
    EXPECT_EQ(repo.updateCalls, 0);
}

// SEC-04: exactly one RECORD_UPDATED row per successful update.
TEST_F(RecordServiceUpdateTest, AuditsRecordUpdated) {
    StudentPatch patch;
    patch.semester = 6;
    ASSERT_EQ(service.updateStudent(kAdmin, "PES1UG24CS116", patch), Status::OK);
    ASSERT_EQ(audits.size(), 1u);
    EXPECT_EQ(audits[0].event, "RECORD_UPDATED");
    EXPECT_EQ(audits[0].username, "admin");
    EXPECT_EQ(audits[0].roll_no, "PES1UG24CS116");
    EXPECT_EQ(audits[0].outcome, "SUCCESS");
}

TEST_F(RecordServiceUpdateTest, StorageFailuresArePassedThroughAndNotAudited) {
    StudentPatch patch;
    patch.semester = 6;
    repo.updateResult = Status::DB_ERROR;
    EXPECT_EQ(service.updateStudent(kAdmin, "PES1UG24CS116", patch), Status::DB_ERROR);
    repo.updateResult = Status::OK;
    repo.findFails = Status::DB_ERROR;
    EXPECT_EQ(service.updateStudent(kAdmin, "PES1UG24CS116", patch), Status::DB_ERROR);
    EXPECT_TRUE(audits.empty());
}

// ---------------------------------------------------------------------------------------------
// US-05 Delete (UC-05; REQ-CRUD-05, 07; NFR-SAFE-01; SEC-04, SEC-07)
// ---------------------------------------------------------------------------------------------
class RecordServiceDeleteTest : public RecordServiceUpdateTest {};

// REQ-CRUD-05: only exactly "YES" confirms.
TEST(RecordServiceConfirm, OnlyExactYesConfirms) {
    EXPECT_TRUE(RecordService::isDeleteConfirmed("YES"));
    for (const char* no : {"yes", "Yes", "Y", "", "NO", " YES", "YES ", "YES\n", "y", "YESS"})
        EXPECT_FALSE(RecordService::isDeleteConfirmed(no)) << "[" << no << "]";
}

TEST_F(RecordServiceDeleteTest, DeletesExistingRecord) {
    EXPECT_EQ(service.deleteStudent(kAdmin, "PES1UG24CS116"), Status::OK);
    EXPECT_TRUE(repo.rows.empty());
}

TEST_F(RecordServiceDeleteTest, RollNoIsFoundIgnoringCase) {
    EXPECT_EQ(service.deleteStudent(kAdmin, "pes1ug24cs116"), Status::OK);
    EXPECT_TRUE(repo.rows.empty());
}

// REQ-CRUD-07: unknown roll_no -> NOT_FOUND, contents unchanged.
TEST_F(RecordServiceDeleteTest, UnknownRollNoIsNotFoundAndChangesNothing) {
    EXPECT_EQ(service.deleteStudent(kAdmin, "PES1UG24CS999"), Status::NOT_FOUND);
    EXPECT_EQ(repo.removeCalls, 0);
    EXPECT_EQ(repo.rows.size(), 1u);
    EXPECT_TRUE(audits.empty());
}

TEST_F(RecordServiceDeleteTest, ViewerAndIdleSessionsAreDenied) {
    EXPECT_EQ(service.deleteStudent(Session{"v", false, kNow}, "PES1UG24CS116"), Status::ACCESS_DENIED);
    EXPECT_EQ(service.deleteStudent(Session{"admin", true, kNow - 601}, "PES1UG24CS116"),
              Status::ACCESS_DENIED);
    EXPECT_EQ(repo.removeCalls, 0);
    EXPECT_EQ(repo.rows.size(), 1u);
}

TEST_F(RecordServiceDeleteTest, AuditsRecordDeleted) {
    ASSERT_EQ(service.deleteStudent(kAdmin, "PES1UG24CS116"), Status::OK);
    ASSERT_EQ(audits.size(), 1u);
    EXPECT_EQ(audits[0].event, "RECORD_DELETED");
    EXPECT_EQ(audits[0].roll_no, "PES1UG24CS116");
    EXPECT_EQ(audits[0].outcome, "SUCCESS");
}

TEST_F(RecordServiceDeleteTest, StorageFailuresArePassedThroughAndNotAudited) {
    repo.removeResult = Status::DB_ERROR;
    EXPECT_EQ(service.deleteStudent(kAdmin, "PES1UG24CS116"), Status::DB_ERROR);
    EXPECT_EQ(repo.rows.size(), 1u);
    repo.removeResult = Status::OK;
    repo.existsFails = Status::DB_ERROR;
    EXPECT_EQ(service.deleteStudent(kAdmin, "PES1UG24CS116"), Status::DB_ERROR);
    EXPECT_TRUE(audits.empty());
}

// getStudent is what the console shows before asking for YES (UC-05 step 3).
TEST_F(RecordServiceDeleteTest, GetStudentReturnsRecordOrNotFound) {
    auto found = service.getStudent(kAdmin, "pes1ug24cs116");
    ASSERT_EQ(found.status, Status::OK);
    EXPECT_EQ(found.value.name, "Bhavyaa Garg");
    EXPECT_EQ(service.getStudent(kAdmin, "PES1UG24CS999").status, Status::NOT_FOUND);
    EXPECT_EQ(service.getStudent(Session{"v", false, kNow}, "PES1UG24CS116").status, Status::ACCESS_DENIED);
}

// ---------------------------------------------------------------------------------------------
// US-06 List (UC-06; REQ-CRUD-06) with 45 records: pages of 20, 20, 5 in roll_no order.
// ---------------------------------------------------------------------------------------------
class RecordServiceListTest : public RecordServiceAddTest {
protected:
    void SetUp() override {
        for (int i = 0; i < 45; ++i) {
            Student s = goodStudent();
            char buf[32];
            std::snprintf(buf, sizeof buf, "PES1UG24CS%03d", 100 + i);
            s.roll_no = buf;
            ASSERT_EQ(service.addStudent(kAdmin, s), Status::OK);
        }
    }
};

TEST_F(RecordServiceListTest, ThreePagesOfTwentyTwentyFive) {
    auto p1 = service.listStudents(kAdmin, 1);
    auto p2 = service.listStudents(kAdmin, 2);
    auto p3 = service.listStudents(kAdmin, 3);
    ASSERT_EQ(p1.status, Status::OK);
    EXPECT_EQ(p1.value.items.size(), 20u);
    EXPECT_EQ(p2.value.items.size(), 20u);
    EXPECT_EQ(p3.value.items.size(), 5u);
    EXPECT_EQ(p1.value.total_pages, 3);
    EXPECT_EQ(p1.value.total_items, 45);
    EXPECT_EQ(p3.value.page, 3);
}

TEST_F(RecordServiceListTest, OrderedByRollNoAscendingAcrossPages) {
    std::vector<std::string> all;
    for (int page = 1; page <= 3; ++page)
        for (const Student& s : service.listStudents(kAdmin, page).value.items) all.push_back(s.roll_no);
    ASSERT_EQ(all.size(), 45u);
    EXPECT_TRUE(std::is_sorted(all.begin(), all.end()));
    EXPECT_EQ(all.front(), "PES1UG24CS100");
    EXPECT_EQ(all.back(), "PES1UG24CS144");
}

TEST_F(RecordServiceListTest, PageOutOfRangeIsValidationError) {
    EXPECT_EQ(service.listStudents(kAdmin, 0).status, Status::VALIDATION_ERROR);
    EXPECT_EQ(service.listStudents(kAdmin, -1).status, Status::VALIDATION_ERROR);
    EXPECT_EQ(service.listStudents(kAdmin, 4).status, Status::VALIDATION_ERROR);
}

TEST_F(RecordServiceListTest, ExactMultipleOfPageSizeHasNoEmptyLastPage) {
    for (int i = 0; i < 5; ++i) {  // 45 - 5 = 40 records = exactly 2 pages
        char buf[32];
        std::snprintf(buf, sizeof buf, "PES1UG24CS%03d", 140 + i);
        ASSERT_EQ(service.deleteStudent(kAdmin, buf), Status::OK);
    }
    auto p = service.listStudents(kAdmin, 1);
    EXPECT_EQ(p.value.total_pages, 2);
    EXPECT_EQ(service.listStudents(kAdmin, 3).status, Status::VALIDATION_ERROR);
}

TEST_F(RecordServiceListTest, ViewerAndIdleSessionsAreDenied) {
    EXPECT_EQ(service.listStudents(Session{"v", false, kNow}, 1).status, Status::ACCESS_DENIED);
    EXPECT_EQ(service.listStudents(Session{"admin", true, kNow - 601}, 1).status, Status::ACCESS_DENIED);
}

TEST_F(RecordServiceListTest, StorageFailuresArePassedThrough) {
    repo.countFails = Status::DB_ERROR;
    EXPECT_EQ(service.listStudents(kAdmin, 1).status, Status::DB_ERROR);
    repo.countFails = Status::OK;
    repo.fetchFails = Status::DB_ERROR;
    EXPECT_EQ(service.listStudents(kAdmin, 1).status, Status::DB_ERROR);
}

TEST(RecordServiceListEmpty, EmptyDatabaseIsOkWithEmptyPageOne) {
    FakeRepo repo;
    RecordService service{repo, nullptr, Validator(), [] { return kNow; }};
    auto p = service.listStudents(kAdmin, 1);
    ASSERT_EQ(p.status, Status::OK);
    EXPECT_TRUE(p.value.items.empty());
    EXPECT_EQ(p.value.total_pages, 1);
    EXPECT_EQ(p.value.total_items, 0);
}

// Defaults: no audit hook and the real clock must both work.
TEST(RecordServiceDefaults, WorksWithoutAuditHookAndWithSystemClock) {
    FakeRepo repo;
    RecordService service{repo};
    Session session{"admin", true, std::time(nullptr)};
    EXPECT_EQ(service.addStudent(session, goodStudent()), Status::OK);
}

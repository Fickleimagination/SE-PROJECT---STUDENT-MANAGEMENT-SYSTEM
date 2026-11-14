// GoogleTest suite for RecordController, the console menu for records
// (SRS UC-03..UC-06; test plan TC-CRUD-01..05, TC-UI-01..03). Typed input goes in through a
// stringstream and the screen text is checked, so no terminal is needed.
#include <gtest/gtest.h>

#include <ctime>
#include <map>
#include <sstream>
#include <string>

#include "record_controller.h"

using namespace srms;

namespace {

class FakeRepo : public StudentRepository {
public:
    std::map<std::string, Student> rows;
    Status insertResult = Status::OK;

    Result<bool> exists(const std::string& id) override { return {Status::OK, rows.count(id) > 0, ""}; }
    Status insert(const Student& s) override {
        if (insertResult != Status::OK) return insertResult;
        rows[s.roll_no] = s;
        return Status::OK;
    }
    Result<Student> find(const std::string& id) override {
        auto it = rows.find(id);
        if (it == rows.end()) return {Status::NOT_FOUND, Student{}, ""};
        return {Status::OK, it->second, ""};
    }
    Status update(const Student& s) override { rows[s.roll_no] = s; return Status::OK; }
    Status remove(const std::string& id) override { return rows.erase(id) ? Status::OK : Status::NOT_FOUND; }
    Result<int> count() override { return {Status::OK, static_cast<int>(rows.size()), ""}; }
    Result<std::vector<Student>> fetch(int offset, int limit) override {
        std::vector<Student> out;
        int i = 0;
        for (const auto& kv : rows) {
            if (i++ < offset) continue;
            if (static_cast<int>(out.size()) == limit) break;
            out.push_back(kv.second);
        }
        return {Status::OK, out, ""};
    }
};

Student sample(const std::string& roll = "PES1UG24CS116") {
    return Student{roll, "Bhavyaa Garg", "CSE", 5, "9876543210", 8.41, "", ""};
}

class RecordControllerTest : public ::testing::Test {
protected:
    FakeRepo repo;
    RecordService service{repo};
    Session admin{"admin", true, std::time(nullptr)};

    // Runs the menu with the given typed lines and returns everything shown on screen.
    std::string run(const std::string& typed, Session* who = nullptr) {
        std::istringstream in(typed);
        std::ostringstream out;
        RecordController controller(service, in, out);
        controller.run(who ? *who : admin);
        return out.str();
    }
    static bool has(const std::string& screen, const std::string& text) {
        return screen.find(text) != std::string::npos;
    }
};

}  // namespace

// ---- menu ------------------------------------------------------------------------------------

TEST_F(RecordControllerTest, ShowsMenuAndReturnsOnBack) {
    std::string screen = run("5\n");
    EXPECT_TRUE(has(screen, "1. Add record"));
    EXPECT_TRUE(has(screen, "4. List all records"));
    EXPECT_TRUE(has(screen, "5. Back"));
}

TEST_F(RecordControllerTest, InvalidChoiceIsRejectedAndMenuReturns) {
    std::string screen = run("9\nabc\n5\n");
    EXPECT_TRUE(has(screen, "Invalid choice. Enter 1, 2, 3, 4, or 5."));
}

TEST_F(RecordControllerTest, EndOfInputLeavesTheMenuInsteadOfLooping) {
    EXPECT_NO_FATAL_FAILURE(run(""));
    EXPECT_NO_FATAL_FAILURE(run("1\nPES1UG24CS116\n"));  // input ends in the middle of Add
}

TEST_F(RecordControllerTest, InputRefreshesSessionActivity) {
    Session old{"admin", true, std::time(nullptr) - 500};
    run("5\n", &old);
    EXPECT_GE(old.last_activity, std::time(nullptr) - 1);
}

// ---- add (UC-03) -----------------------------------------------------------------------------

TEST_F(RecordControllerTest, AddsRecordAndConfirmsWithRollNumber) {
    std::string screen = run("1\nPES1UG24CS116\nBhavyaa Garg\nCSE\n5\n9876543210\n8.41\n5\n");
    EXPECT_TRUE(has(screen, "Record PES1UG24CS116 added successfully."));
    ASSERT_EQ(repo.rows.count("PES1UG24CS116"), 1u);
    EXPECT_EQ(repo.rows.at("PES1UG24CS116").name, "Bhavyaa Garg");
    EXPECT_DOUBLE_EQ(repo.rows.at("PES1UG24CS116").cgpa, 8.41);
}

TEST_F(RecordControllerTest, PromptsInTheOrderTheSrsGives) {
    std::string screen = run("1\nPES1UG24CS116\nBhavyaa Garg\nCSE\n5\n\n8.41\n5\n");
    auto at = [&](const char* s) { return screen.find(s); };
    EXPECT_LT(at("Roll number:"), at("Name:"));
    EXPECT_LT(at("Name:"), at("Department"));
    EXPECT_LT(at("Department"), at("Semester"));
    EXPECT_LT(at("Semester"), at("Contact"));
    EXPECT_LT(at("Contact"), at("CGPA"));
}

TEST_F(RecordControllerTest, ContactCanBeSkippedWithEnter) {
    std::string screen = run("1\nPES1UG24CS116\nBhavyaa Garg\nCSE\n5\n\n8.41\n5\n");
    EXPECT_TRUE(has(screen, "added successfully"));
    EXPECT_EQ(repo.rows.at("PES1UG24CS116").contact, "");
}

// TC-CRUD-02: duplicate roll_no.
TEST_F(RecordControllerTest, DuplicateRollNumberShowsSrsError) {
    repo.rows["PES1UG24CS116"] = sample();
    std::string screen = run("1\nPES1UG24CS116\nSomeone Else\nCSE\n5\n\n7.0\n5\n");
    EXPECT_TRUE(has(screen, "ERROR: roll_no: PES1UG24CS116 already exists."));
    EXPECT_EQ(repo.rows.at("PES1UG24CS116").name, "Bhavyaa Garg");
}

// UC-03 3a: only the failing field is asked again.
TEST_F(RecordControllerTest, BadFieldIsAskedAgainWithFieldSpecificError) {
    std::string screen = run("1\nPES1UG24CS116\nBhavyaa Garg\nCSE\n9\n5\n\n8.41\n5\n");
    EXPECT_TRUE(has(screen, "ERROR: semester: must be a whole number from 1 to 8."));
    EXPECT_TRUE(has(screen, "added successfully"));
    EXPECT_EQ(repo.rows.at("PES1UG24CS116").semester, 5);
}

TEST_F(RecordControllerTest, ThreeWrongAttemptsCancelTheOperation) {
    std::string screen = run("1\nAB1\nAB2\nAB3\n5\n");
    EXPECT_TRUE(has(screen, "ERROR: roll_no: must be 5-20 letters or digits."));
    EXPECT_TRUE(has(screen, "Operation cancelled."));
    EXPECT_TRUE(repo.rows.empty());
}

TEST_F(RecordControllerTest, StorageFailureShowsRecordNotSaved) {
    repo.insertResult = Status::DB_ERROR;
    std::string screen = run("1\nPES1UG24CS116\nBhavyaa Garg\nCSE\n5\n\n8.41\n5\n");
    EXPECT_TRUE(has(screen, "ERROR: database: record not saved."));
}

TEST_F(RecordControllerTest, ViewerSessionCannotAdd) {
    Session viewer{"viewer", false, std::time(nullptr)};
    std::string screen = run("1\nPES1UG24CS116\nBhavyaa Garg\nCSE\n5\n\n8.41\n5\n", &viewer);
    EXPECT_TRUE(has(screen, "ERROR: access: administrator login required."));
    EXPECT_TRUE(repo.rows.empty());
}

// ---- update (UC-04) --------------------------------------------------------------------------

TEST_F(RecordControllerTest, UpdateChangesTypedFieldsAndKeepsTheRestOnEnter) {
    repo.rows["PES1UG24CS116"] = sample();
    std::string screen = run("2\nPES1UG24CS116\n\n\n6\n\n9.10\n5\n");
    EXPECT_TRUE(has(screen, "Record PES1UG24CS116 updated successfully."));
    const Student& s = repo.rows.at("PES1UG24CS116");
    EXPECT_EQ(s.name, "Bhavyaa Garg");
    EXPECT_EQ(s.department, "CSE");
    EXPECT_EQ(s.semester, 6);
    EXPECT_EQ(s.contact, "9876543210");
    EXPECT_DOUBLE_EQ(s.cgpa, 9.10);
}

TEST_F(RecordControllerTest, UpdateShowsCurrentRecordAndValuesInBrackets) {
    repo.rows["PES1UG24CS116"] = sample();
    std::string screen = run("2\nPES1UG24CS116\n\n\n\n\n\n5\n");
    EXPECT_TRUE(has(screen, "Name [Bhavyaa Garg]:"));
    EXPECT_TRUE(has(screen, "CGPA [8.41]:"));
}

TEST_F(RecordControllerTest, UpdateOfUnknownRollNumberShowsNotFound) {
    std::string screen = run("2\nPES1UG24CS999\n5\n");
    EXPECT_TRUE(has(screen, "ERROR: roll_no: record not found."));
}

TEST_F(RecordControllerTest, UpdateRejectsBadValueAndKeepsStoredRecord) {
    repo.rows["PES1UG24CS116"] = sample();
    std::string screen = run("2\nPES1UG24CS116\nR2D2\nR2D2\nR2D2\n5\n");
    EXPECT_TRUE(has(screen, "Operation cancelled."));
    EXPECT_EQ(repo.rows.at("PES1UG24CS116").name, "Bhavyaa Garg");
}

// ---- delete (UC-05) --------------------------------------------------------------------------

// TC-CRUD-04: only exactly "YES" deletes.
TEST_F(RecordControllerTest, DeleteWithExactYesRemovesRecord) {
    repo.rows["PES1UG24CS116"] = sample();
    std::string screen = run("3\nPES1UG24CS116\nYES\n5\n");
    EXPECT_TRUE(has(screen, "Type YES to confirm deletion of PES1UG24CS116:"));
    EXPECT_TRUE(has(screen, "Record PES1UG24CS116 deleted."));
    EXPECT_TRUE(repo.rows.empty());
}

TEST_F(RecordControllerTest, AnythingOtherThanExactYesCancelsDeletion) {
    for (const char* answer : {"yes", "Y", "", "NO", " YES", "YES "}) {
        repo.rows["PES1UG24CS116"] = sample();
        std::string screen = run(std::string("3\nPES1UG24CS116\n") + answer + "\n5\n");
        EXPECT_TRUE(has(screen, "Deletion cancelled.")) << "[" << answer << "]";
        EXPECT_EQ(repo.rows.size(), 1u) << "[" << answer << "]";
    }
}

TEST_F(RecordControllerTest, DeleteOfUnknownRollNumberShowsNotFound) {
    std::string screen = run("3\nPES1UG24CS999\n5\n");
    EXPECT_TRUE(has(screen, "ERROR: roll_no: record not found."));
    EXPECT_FALSE(has(screen, "Type YES"));
}

TEST_F(RecordControllerTest, DeleteShowsTheRecordBeforeAskingForConfirmation) {
    repo.rows["PES1UG24CS116"] = sample();
    std::string screen = run("3\nPES1UG24CS116\nNO\n5\n");
    EXPECT_LT(screen.find("Bhavyaa Garg"), screen.find("Type YES"));
}

// ---- list (UC-06) ----------------------------------------------------------------------------

TEST_F(RecordControllerTest, EmptyDatabaseSaysNoRecords) {
    EXPECT_TRUE(has(run("4\n5\n"), "No records available."));
}

TEST_F(RecordControllerTest, SinglePageListShowsColumnsAndRecords) {
    repo.rows["PES1UG24CS116"] = sample();
    std::string screen = run("4\n5\n");
    EXPECT_TRUE(has(screen, "Roll no"));
    EXPECT_TRUE(has(screen, "PES1UG24CS116"));
    EXPECT_TRUE(has(screen, "8.41"));
    EXPECT_TRUE(has(screen, "Page 1 of 1 (1 records)"));
}

// TC-CRUD-05: 45 records give pages of 20, 20 and 5.
TEST_F(RecordControllerTest, FortyFiveRecordsPageThroughThreePages) {
    for (int i = 0; i < 45; ++i) {
        char id[32];
        std::snprintf(id, sizeof id, "PES1UG24CS%03d", 100 + i);
        repo.rows[id] = sample(id);
    }
    std::string screen = run("4\nN\nN\nN\nP\nQ\n5\n");
    EXPECT_TRUE(has(screen, "Page 1 of 3 (45 records)"));
    EXPECT_TRUE(has(screen, "Page 2 of 3"));
    EXPECT_TRUE(has(screen, "Page 3 of 3"));
    EXPECT_TRUE(has(screen, "Already on the last page."));
}

TEST_F(RecordControllerTest, PreviousOnFirstPageIsRefused) {
    for (int i = 0; i < 25; ++i) {
        char id[32];
        std::snprintf(id, sizeof id, "PES1UG24CS%03d", 100 + i);
        repo.rows[id] = sample(id);
    }
    EXPECT_TRUE(has(run("4\nP\nQ\n5\n"), "Already on the first page."));
}

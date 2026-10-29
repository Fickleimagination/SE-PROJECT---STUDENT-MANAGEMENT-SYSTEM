#include "search_service.h"

#include <gtest/gtest.h>

#include <ctime>
#include <optional>
#include <string>
#include <vector>

namespace srms {
namespace {

class FakeSearchRepository final : public SearchRepository {
public:
    Result<std::vector<Student>> response{
        Status::OK, {}, ""
    };

    bool called = false;
    bool allow_name_search = false;
    std::string received_term;

    Result<std::vector<Student>> search(
        const std::string& term,
        bool allow_names
    ) override {
        called = true;
        received_term = term;
        allow_name_search = allow_names;
        return response;
    }
};

Student makeStudent() {
    Student student{};

    student.roll_no = "CS12345";
    student.name = "Varun Kumar";
    student.department = "CSE";
    student.semester = 3;
    student.contact = "9876543210";
    student.cgpa = 8.5;
    student.created_at = "2026-10-20 10:00:00";
    student.updated_at = "2026-10-20 10:00:00";

    return student;
}

TEST(SearchServiceTest, ViewerGetsOnlyPublicFields) {
    FakeSearchRepository repository;
    repository.response = {
        Status::OK, {makeStudent()}, ""
    };

    const std::time_t now = 1000;
    SearchService service(repository, [now]() { return now; });

    const std::optional<Session> viewer =
        Session{"viewer", false, now};

    const auto result = service.search(viewer, "CS12345");

    ASSERT_EQ(result.status, Status::OK);
    ASSERT_EQ(result.value.items.size(), 1U);

    EXPECT_FALSE(repository.allow_name_search);

    const auto& student = result.value.items.front();

    EXPECT_TRUE(student.contact.empty());
    EXPECT_TRUE(student.created_at.empty());
    EXPECT_TRUE(student.updated_at.empty());
    EXPECT_EQ(result.value.total_items, 1);
}

TEST(SearchServiceTest, AdministratorCanSearchPartialName) {
    FakeSearchRepository repository;
    repository.response = {
        Status::OK, {makeStudent()}, ""
    };

    const std::time_t now = 1000;
    SearchService service(repository, [now]() { return now; });

    const std::optional<Session> admin =
        Session{"admin", true, now};

    const auto result = service.search(admin, "var");

    EXPECT_EQ(result.status, Status::OK);
    EXPECT_TRUE(repository.called);
    EXPECT_TRUE(repository.allow_name_search);
    EXPECT_EQ(repository.received_term, "var");
}

TEST(SearchServiceTest, RejectsOneCharacterAdminSearch) {
    FakeSearchRepository repository;

    const std::time_t now = 1000;
    SearchService service(repository, [now]() { return now; });

    const std::optional<Session> admin =
        Session{"admin", true, now};

    const auto result = service.search(admin, "v");

    EXPECT_EQ(result.status, Status::VALIDATION_ERROR);
    EXPECT_FALSE(repository.called);
}

TEST(SearchServiceTest, EmptyResultsShowRequiredMessage) {
    FakeSearchRepository repository;

    const std::time_t now = 1000;
    SearchService service(repository, [now]() { return now; });

    const std::optional<Session> viewer =
        Session{"viewer", false, now};

    const auto result = service.search(viewer, "CS99999");

    EXPECT_EQ(result.status, Status::OK);
    EXPECT_TRUE(result.value.items.empty());
    EXPECT_EQ(result.detail, "No matching record found.");
    EXPECT_EQ(result.value.total_items, 0);
}

TEST(SearchServiceTest, RejectsExpiredSession) {
    FakeSearchRepository repository;

    const std::time_t now = 2000;
    SearchService service(repository, [now]() { return now; });

    const std::optional<Session> admin =
        Session{"admin", true, now - 601};

    const auto result = service.search(admin, "var");

    EXPECT_EQ(result.status, Status::ACCESS_DENIED);
    EXPECT_FALSE(repository.called);
}

TEST(SearchServiceTest, PropagatesRepositoryDatabaseError) {
    FakeSearchRepository repository;
    repository.response = {
        Status::DB_ERROR, {}, "database query failed"
    };

    const std::time_t now = 1000;
    SearchService service(repository, [now]() { return now; });

    const std::optional<Session> admin =
        Session{"admin", true, now};

    const auto result = service.search(admin, "var");

    EXPECT_EQ(result.status, Status::DB_ERROR);
    EXPECT_EQ(result.detail, "database query failed");
}

}  // namespace
}  // namespace srms
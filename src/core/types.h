
// Shared types (SAD 4.3.1). Owner: Bhavyaa Garg.
#pragma once

#include <ctime>
#include <optional>
#include <string>
#include <vector>

namespace srms {

enum class Status {
    OK,
    VALIDATION_ERROR,
    DUPLICATE_KEY,
    NOT_FOUND,
    ACCESS_DENIED,
    ACCOUNT_LOCKED,
    CANCELLED,
    DB_ERROR,
    IO_ERROR,
    INTERNAL_ERROR
};

struct Session {
    std::string username;
    bool is_admin = false;
    std::time_t last_activity = 0;
};

struct Student {
    std::string roll_no;
    std::string name;
    std::string department;
    int semester = 0;
    std::string contact;
    double cgpa = 0.0;
    std::string created_at;
    std::string updated_at;
};

struct StudentPatch {
    std::optional<std::string> name;
    std::optional<std::string> department;
    std::optional<std::string> contact;
    std::optional<int> semester;
    std::optional<double> cgpa;
};

enum class Field {
    Department,
    Semester,
    Cgpa
};

enum class Order {
    Ascending,
    Descending
};

struct FilterCriteria {
    std::optional<std::string> department;
    std::optional<int> semester;
};

template <typename T>
struct Result {
    Status status = Status::INTERNAL_ERROR;
    T value{}; // Added default member initializer to satisfy cppcheck
    std::string detail;
};

template <typename T>
struct Page {
    std::vector<T> items;
    int page = 1;
    int total_pages = 0;
    int total_items = 0;
};

}  // namespace srms

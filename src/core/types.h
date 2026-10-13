// TEMPORARY stub of the shared types from SAD Section 4.3.1.
// Owner: Bhavyaa Garg. Replace this file with the team's version when merged;
// keep the names and field order identical so report_service compiles unchanged.
#pragma once
#include <ctime>
#include <string>
#include <vector>

namespace srms {

enum class Status {
    OK, VALIDATION_ERROR, DUPLICATE_KEY, NOT_FOUND, ACCESS_DENIED,
    ACCOUNT_LOCKED, CANCELLED, DB_ERROR, IO_ERROR, INTERNAL_ERROR
};

struct Session {
    std::string username;
    bool is_admin;
    std::time_t last_activity;
};

struct Student {
    std::string roll_no;
    std::string name;
    std::string department;
    int semester;
    std::string contact;
    double cgpa;
    std::string created_at;
    std::string updated_at;
};

template <typename T>
struct Result {
    Status status;
    T value;
    std::string detail;
};

template <typename T>
struct Page {
    std::vector<T> items;
    int page;
    int total_pages;
    int total_items;
};

}  // namespace srms

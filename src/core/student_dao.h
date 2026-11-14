
#pragma once

#include "database.h"
#include "record_service.h"
#include "report_service.h"
#include "search_service.h"

#include <string>
#include <vector>

namespace srms {

class StudentDAO final
    : public StudentRepository,
      public StudentSource,
      public SearchRepository {
public:
    explicit StudentDAO(Database& database) noexcept
        : database_(database) {}

    // US-07: Persistence and duplicate detection.
    Result<bool> exists(
        const std::string& roll_no
    ) override;

    Status insert(const Student& student) override;

    // StudentRepository CRUD and pagination methods.
    Result<Student> find(const std::string& roll_no) override;

    Status update(const Student& student) override;

    Status remove(const std::string& roll_no) override;

    Result<int> count() override;

    Result<std::vector<Student>> fetch(
        int offset,
        int limit
    ) override;

    // Existing report-support interface.
    Result<std::vector<Student>> findByDeptSem(
    const std::string& department,
    int semester
    ) override;

    // US-08: Search by roll number or name.
    Result<std::vector<Student>> search(
        const std::string& term,
        bool allow_name_search
    ) override;

        Result<std::vector<Student>> sortRecords(
        Field field,
        Order order
    ) override;

    Result<std::vector<Student>> filterRecords(
        const FilterCriteria& criteria
    ) override;

private:
    Database& database_;
};

}  // namespace srms

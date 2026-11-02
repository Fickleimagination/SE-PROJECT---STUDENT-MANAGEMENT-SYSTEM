
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

private:
    Database& database_;
};

}  // namespace srms

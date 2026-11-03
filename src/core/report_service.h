// Reports & Statistics (owner: Arabhi Bhat). SRS 5.4, SAD 4.2.6 and 4.3.2.
// Console-free: no <iostream>, printf or scanf here (NFR-MNT-01).
#pragma once
#include <ctime>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "types.h"

namespace srms {

enum class ReportType { ClassList, AverageCgpa, TopperList };
enum class Format { TXT, CSV };

struct Filters {
    std::string department;  // one of the valid codes (REQ-DATA-05)
    int semester;            // 1..8 (REQ-DATA-04)
};

struct ReportRow {
    std::string roll_no;
    std::string name;
    std::string department;
    int semester;
    double cgpa;
};

struct Report {
    ReportType type = ReportType::ClassList;
    std::string department;
    int semester = 0;
    std::vector<ReportRow> rows;  // ClassList: all students; TopperList: all tied toppers
    int student_count = 0;        // used by AverageCgpa and the empty-report check
    double average = 0.0;         // AverageCgpa only, rounded to 2 dp (REQ-RPT-02)
    double highest = 0.0;         // TopperList only
    std::time_t generated_at = 0;
};

// Read-only data source; implemented by StudentDAO (SRS/SAD: parameterized SQL only).
class StudentSource {
public:
    virtual ~StudentSource() = default;
    virtual Result<std::vector<Student>> findByDeptSem(
    const std::string& department,
    int semester
    ) = 0;
};

// Called after a successful export so the audit_log records REPORT_EXPORTED (SEC-04).
using AuditHook = std::function<void(const std::string& event, const std::string& outcome)>;

class ReportService {
public:
    ReportService(StudentSource& source, AuditHook audit);

    Result<Report> generate(const Session& session, ReportType type,
                            const Filters& filters, std::time_t now);

    // Writes ./reports/<type>_<YYYYMMDD_HHMMSS>.txt or .csv and returns its absolute path.
    Result<std::string> exportToFile(const Session& session, const Report& report,
                                     Format format, std::time_t now,
                                     const std::string& base_dir = "./reports");

private:
    StudentSource& source_;
    AuditHook audit_;
};

std::string reportTypeName(ReportType type);

// Header plus data rows for display or export. Shared by TXT and CSV writers.
std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>>
buildTable(const Report& report);

}  // namespace srms

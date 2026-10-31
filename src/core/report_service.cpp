#include "report_service.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace srms {
namespace {

constexpr std::time_t kIdleLimitSeconds = 600;  // REQ-AUTH-08: 10 minutes idle
const std::vector<std::string> kDepartments = {"CSE", "ECE", "EEE", "ME", "CV", "BT", "AIML"};

// CGPA is stored as a double; compare in hundredths so ties are exact (REQ-RPT-03).
long long hundredths(double value) { return std::llround(value * 100.0); }

std::string formatTime(std::time_t t, const char* fmt) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}

std::string fixed2(double value) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", value);
    return buf;
}

std::string csvEscape(const std::string& field) {
    if (field.find_first_of(",\"\n\r") == std::string::npos) return field;
    std::string out = "\"";
    for (char c : field) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    return out + "\"";
}
bool sessionAllowed(const Session& session, std::time_t now) {
    if (!session.is_admin) return false;
    if (session.last_activity > now) return false;  // future timestamp: reject
    return (now - session.last_activity) <= kIdleLimitSeconds;
}
}  // namespace

ReportService::ReportService(StudentSource& source, AuditHook audit)
    : source_(source), audit_(std::move(audit)) {}

std::string reportTypeName(ReportType type) {
    switch (type) {
        case ReportType::ClassList: return "class_list";
        case ReportType::AverageCgpa: return "average_cgpa";
        case ReportType::TopperList: return "topper_list";
    }
    return "report";
}

Result<Report> ReportService::generate(const Session& session, ReportType type,
                                       const Filters& filters, std::time_t now) {
    // REQ-AUTH-06 / SEC-07: check the session inside the service, not only in the menu.
    if (!sessionAllowed(session, now)) {
        return {Status::ACCESS_DENIED, Report{}, "access: administrator login required."};
    }
    if (std::find(kDepartments.begin(), kDepartments.end(), filters.department) == kDepartments.end()) {
        return {Status::VALIDATION_ERROR, Report{}, "ERROR: department: must be one of the valid codes."};
    }
    if (filters.semester < 1 || filters.semester > 8) {
        return {Status::VALIDATION_ERROR, Report{}, "ERROR: semester: must be an integer from 1 to 8."};
    }

    Report report;
    report.type = type;
    report.department = filters.department;
    report.semester = filters.semester;
    report.generated_at = now;

    std::vector<Student> students = source_.findByDeptSem(filters.department, filters.semester);
    report.student_count = static_cast<int>(students.size());

    if (students.empty()) {
        // REQ-RPT-05: the message is shown and no file is created on export.
        return {Status::OK, report, "No records available for this report."};
    }

    std::sort(students.begin(), students.end(),
              [](const Student& a, const Student& b) { return a.roll_no < b.roll_no; });

    auto toRow = [](const Student& s) {
        return ReportRow{s.roll_no, s.name, s.department, s.semester, s.cgpa};
    };

    switch (type) {
        case ReportType::ClassList:
            for (const auto& s : students) report.rows.push_back(toRow(s));
            break;

        case ReportType::AverageCgpa: {
            double sum = 0.0;
            for (const auto& s : students) sum += s.cgpa;
            report.average = std::round(sum / students.size() * 100.0) / 100.0;
            break;
        }

        case ReportType::TopperList: {
            double top = students.front().cgpa;
            for (const auto& s : students) top = std::max(top, s.cgpa);
            report.highest = top;
            // REQ-RPT-03: every student tied for the highest CGPA, not just one.
            for (const auto& s : students) {
                if (hundredths(s.cgpa) == hundredths(top)) report.rows.push_back(toRow(s));
            }
            break;
        }
    }
    return {Status::OK, report, ""};
}

Result<std::string> ReportService::exportToFile(const Session& session, const Report& report,
                                                Format format, std::time_t now,
                                                const std::string& base_dir) {
    if (!sessionAllowed(session, now)) {
        return {Status::ACCESS_DENIED, "", "access: administrator login required."};
    }
    if (report.student_count == 0) {
        return {Status::NOT_FOUND, "", "No records available for this report."};
    }

    const std::string ext = (format == Format::CSV) ? ".csv" : ".txt";
    const std::string file_name =
        reportTypeName(report.type) + "_" + formatTime(now, "%Y%m%d_%H%M%S") + ext;
    const std::filesystem::path dir(base_dir);
    const std::filesystem::path path = dir / file_name;

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return {Status::IO_ERROR, "", "ERROR: file: could not write " + file_name + "."};
    }

    auto [header, table] = buildTable(report);
    const char sep = (format == Format::CSV) ? ',' : '\t';

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        return {Status::IO_ERROR, "", "ERROR: file: could not write " + file_name + "."};
    }
    auto writeLine = [&](const std::vector<std::string>& fields) {
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i) out << sep;
            out << (format == Format::CSV ? csvEscape(fields[i]) : fields[i]);
        }
        out << '\n';
    };
    writeLine(header);
    for (const auto& row : table) writeLine(row);
    out.close();
    if (!out) {
        return {Status::IO_ERROR, "", "ERROR: file: could not write " + file_name + "."};
    }

    if (audit_) audit_("REPORT_EXPORTED", "OK");
    return {Status::OK, std::filesystem::absolute(path).string(), ""};
}

std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>>
buildTable(const Report& report) {
    if (report.type == ReportType::AverageCgpa) {
        return {
            {"department", "semester", "number_of_students", "average_cgpa", "generated_at"},
            {{report.department, std::to_string(report.semester),
              std::to_string(report.student_count), fixed2(report.average),
              formatTime(report.generated_at, "%Y-%m-%d %H:%M:%S")}}};
    }
    std::vector<std::vector<std::string>> table;
    for (const auto& r : report.rows) {
        table.push_back({r.roll_no, r.name, r.department, std::to_string(r.semester), fixed2(r.cgpa)});
    }
    return {{"roll_no", "name", "department", "semester", "cgpa"}, table};
}

}  // namespace srms

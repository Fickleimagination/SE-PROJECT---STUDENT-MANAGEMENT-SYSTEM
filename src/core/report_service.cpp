
#include "report_service.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace srms {
namespace {

constexpr std::time_t kIdleLimitSeconds = 600;

const std::vector<std::string> kDepartments = {
    "CSE", "ECE", "EEE", "ME", "CV", "BT", "AIML"
};

// CGPA is compared in hundredths so ties are handled consistently.
long long hundredths(double value) {
    return std::llround(value * 100.0);
}

std::string formatTime(std::time_t time, const char* format) {
    std::tm timeInfo{};

#if defined(_WIN32)
    if (localtime_s(&timeInfo, &time) != 0) {
        return {};
    }
#else
    if (localtime_r(&time, &timeInfo) == nullptr) {
        return {};
    }
#endif

    char buffer[32]{};

    if (std::strftime(
            buffer,
            sizeof(buffer),
            format,
            &timeInfo
        ) == 0) {
        return {};
    }

    return buffer;
}

std::string fixed2(double value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

std::string csvEscape(const std::string& field) {
    if (field.find_first_of(",\"\n\r") == std::string::npos) {
        return field;
    }

    std::string escaped = "\"";

    for (char ch : field) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped += ch;
        }
    }

    escaped += '"';
    return escaped;
}

bool sessionAllowed(
    const Session& session,
    std::time_t now
) {
    return session.is_admin &&
        now >= session.last_activity &&
        now - session.last_activity <= kIdleLimitSeconds;
}

}  // namespace

ReportService::ReportService(
    StudentSource& source,
    AuditHook audit
)
    : source_(source),
      audit_(std::move(audit)) {}

std::string reportTypeName(ReportType type) {
    switch (type) {
        case ReportType::ClassList:
            return "class_list";

        case ReportType::AverageCgpa:
            return "average_cgpa";

        case ReportType::TopperList:
            return "topper_list";
    }

    return "report";
}

Result<Report> ReportService::generate(
    const Session& session,
    ReportType type,
    const Filters& filters,
    std::time_t now
) {
    // Check authorization inside the service, not just in the UI.
    if (!sessionAllowed(session, now)) {
        return {
            Status::ACCESS_DENIED,
            Report{},
            "access: administrator login required."
        };
    }

    if (std::find(
            kDepartments.begin(),
            kDepartments.end(),
            filters.department
        ) == kDepartments.end()) {
        return {
            Status::VALIDATION_ERROR,
            Report{},
            "ERROR: department: must be one of the valid codes."
        };
    }

    if (filters.semester < 1 || filters.semester > 8) {
        return {
            Status::VALIDATION_ERROR,
            Report{},
            "ERROR: semester: must be an integer from 1 to 8."
        };
    }

    Report report;
    report.type = type;
    report.department = filters.department;
    report.semester = filters.semester;
    report.generated_at = now;

    // The data source now returns Result<vector<Student>>,
    // so database failures are distinguishable from empty results.
    auto studentsResult = source_.findByDeptSem(
        filters.department,
        filters.semester
    );

    if (studentsResult.status != Status::OK) {
        return {
            studentsResult.status,
            report,
            studentsResult.detail
        };
    }

    std::vector<Student> students =
        std::move(studentsResult.value);

    report.student_count =
        static_cast<int>(students.size());

    if (students.empty()) {
        return {
            Status::OK,
            report,
            "No records available for this report."
        };
    }

    std::sort(
        students.begin(),
        students.end(),
        [](const Student& left, const Student& right) {
            return left.roll_no < right.roll_no;
        }
    );

    const auto toRow = [](const Student& student) {
        return ReportRow{
            student.roll_no,
            student.name,
            student.department,
            student.semester,
            student.cgpa
        };
    };

    switch (type) {
        case ReportType::ClassList:
            for (const auto& student : students) {
                report.rows.push_back(toRow(student));
            }
            break;

        case ReportType::AverageCgpa: {
            double sum = 0.0;

            for (const auto& student : students) {
                sum += student.cgpa;
            }

            report.average =
                std::round(
                    (sum / static_cast<double>(students.size()))
                    * 100.0
                ) / 100.0;

            break;
        }

        case ReportType::TopperList: {
            double top = students.front().cgpa;

            for (const auto& student : students) {
                top = std::max(top, student.cgpa);
            }

            report.highest = top;

            // Include every student tied for the highest CGPA.
            for (const auto& student : students) {
                if (hundredths(student.cgpa) == hundredths(top)) {
                    report.rows.push_back(toRow(student));
                }
            }

            break;
        }
    }

    return {
        Status::OK,
        report,
        ""
    };
}

Result<std::string> ReportService::exportToFile(
    const Session& session,
    const Report& report,
    Format format,
    std::time_t now,
    const std::string& base_dir
) {
    if (!sessionAllowed(session, now)) {
        return {
            Status::ACCESS_DENIED,
            "",
            "access: administrator login required."
        };
    }

    if (report.student_count == 0) {
        return {
            Status::NOT_FOUND,
            "",
            "No records available for this report."
        };
    }

    const std::string extension =
        format == Format::CSV ? ".csv" : ".txt";

    const std::string fileName =
        reportTypeName(report.type) + "_" +
        formatTime(now, "%Y%m%d_%H%M%S") +
        extension;

    if (fileName.find_first_of("/\\") != std::string::npos) {
        return {
            Status::IO_ERROR,
            "",
            "ERROR: file: could not generate report filename."
        };
    }

    const std::filesystem::path directory(base_dir);
    const std::filesystem::path path = directory / fileName;

    std::error_code error;

    std::filesystem::create_directories(directory, error);

    if (error) {
        return {
            Status::IO_ERROR,
            "",
            "ERROR: file: could not write " + fileName + "."
        };
    }

    auto [header, table] = buildTable(report);

    const char separator =
        format == Format::CSV ? ',' : '\t';

    std::ofstream output(
        path,
        std::ios::out | std::ios::trunc
    );

    if (!output) {
        return {
            Status::IO_ERROR,
            "",
            "ERROR: file: could not write " + fileName + "."
        };
    }

    const auto writeLine =
        [&](const std::vector<std::string>& fields) {
            for (std::size_t i = 0; i < fields.size(); ++i) {
                if (i > 0) {
                    output << separator;
                }

                output << (
                    format == Format::CSV
                        ? csvEscape(fields[i])
                        : fields[i]
                );
            }

            output << '\n';
        };

    writeLine(header);

    for (const auto& row : table) {
        writeLine(row);
    }

    output.close();

    if (!output) {
        return {
            Status::IO_ERROR,
            "",
            "ERROR: file: could not write " + fileName + "."
        };
    }

    if (audit_) {
        audit_("REPORT_EXPORTED", "OK");
    }

    return {
        Status::OK,
        std::filesystem::absolute(path).string(),
        ""
    };
}

std::pair<
    std::vector<std::string>,
    std::vector<std::vector<std::string>>
> buildTable(const Report& report) {
    if (report.type == ReportType::AverageCgpa) {
        return {
            {
                "department",
                "semester",
                "number_of_students",
                "average_cgpa",
                "generated_at"
            },
            {
                {
                    report.department,
                    std::to_string(report.semester),
                    std::to_string(report.student_count),
                    fixed2(report.average),
                    formatTime(
                        report.generated_at,
                        "%Y-%m-%d %H:%M:%S"
                    )
                }
            }
        };
    }

    std::vector<std::vector<std::string>> table;

    for (const auto& row : report.rows) {
        table.push_back({
            row.roll_no,
            row.name,
            row.department,
            std::to_string(row.semester),
            fixed2(row.cgpa)
        });
    }

    return {
        {
            "roll_no",
            "name",
            "department",
            "semester",
            "cgpa"
        },
        table
    };
}

}  // namespace srms

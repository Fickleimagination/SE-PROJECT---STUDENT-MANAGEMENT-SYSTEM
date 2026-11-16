// Entry point: wires the services into the console menu.
#include <ctime>
#include <sqlite3.h>
#include <iostream>
#include <string>

#include "admin_dao.h"
#include "auth_service.h"
#include "console_ui.h"
#include "database.h"
#include "record_controller.h"
#include "record_service.h"
#include "report_service.h"
#include "student_dao.h"

namespace {

// Username of the admin who is exporting right now (used by the audit log).
std::string g_currentUser;

// Writes one row to audit_log (SEC-04). Uses parameterised SQL (SEC-02).
void writeAudit(srms::Database& database, const std::string& event, const std::string& outcome) {
    std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO audit_log (timestamp, username, event_type, target_roll_no, outcome) "
        "VALUES (?, ?, ?, NULL, ?);";
    if (sqlite3_prepare_v2(database.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(stmt, 1, stamp, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, g_currentUser.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, event.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, outcome.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// Writes one audit_log row for a record change (SEC-04): who did it and to which roll_no.
void writeRecordAudit(srms::Database& database, const std::string& event,
                      const std::string& username, const std::string& rollNo,
                      const std::string& outcome) {
    std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO audit_log (timestamp, username, event_type, target_roll_no, outcome) "
        "VALUES (?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(database.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(stmt, 1, stamp, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, event.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, rollNo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, outcome.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::string ask(const std::string& prompt) {
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    return line;
}

void printTable(const srms::Report& report) {
    auto [header, rows] = srms::buildTable(report);
    for (const auto& h : header) {
        std::cout << h << '\t';
    }
    std::cout << '\n';
    for (const auto& row : rows) {
        for (const auto& cell : row) {
            std::cout << cell << '\t';
        }
        std::cout << '\n';
    }
}

void runReports(srms::Session& session, srms::ReportService& reports) {
    std::cout << "\n===== Reports and statistics =====\n"
              << "1. Class list\n"
              << "2. Average CGPA\n"
              << "3. Topper list\n"
              << "4. Back\n";

    const std::string choice = ask("Enter your choice: ");
    srms::ReportType type;
    if (choice == "1") {
        type = srms::ReportType::ClassList;
    } else if (choice == "2") {
        type = srms::ReportType::AverageCgpa;
    } else if (choice == "3") {
        type = srms::ReportType::TopperList;
    } else {
        return;
    }

    srms::Filters filters{ask("Department code: "), 0};
    try {
        filters.semester = std::stoi(ask("Semester (1-8): "));
    } catch (...) {
        std::cout << "Semester must be a number.\n";
        return;
    }

    auto result = reports.generate(session, type, filters, std::time(nullptr));
    if (result.status != srms::Status::OK) {
        std::cout << result.detail << '\n';
        return;
    }

    const srms::Report& report = result.value;
    if (report.rows.empty() && report.student_count == 0) {
        std::cout << "No records available for this report.\n";
        return;
    }
    if (type == srms::ReportType::AverageCgpa) {
        std::cout << "Students: " << report.student_count << '\n'
                  << "Average CGPA: " << report.average << '\n';
    } else if (type == srms::ReportType::TopperList) {
        printTable(report);
        std::cout << "Highest CGPA: " << report.highest << '\n';
    } else {
        printTable(report);
    }

    const std::string format = ask("Export to file? Type txt, csv or no: ");
    if (format == "txt" || format == "csv") {
        g_currentUser = session.username;
        auto path = reports.exportToFile(
            session, report,
            format == "txt" ? srms::Format::TXT : srms::Format::CSV,
            std::time(nullptr));
        if (path.status == srms::Status::OK) {
            std::cout << "Saved to " << path.value << '\n';
        } else {
            std::cout << "Export failed: " << path.detail << '\n';
        }
    }
}

}  // namespace

int main() {
    srms::Database database;
    if (!database.open("srms.db", "sql/schema.sql")) {
        std::cerr << "Could not open the database.\n";
        return 1;
    }

    srms::AdminDAO adminDao(database);
    srms::StudentDAO studentDao(database);

    srms::AuthService auth(adminDao);
    srms::RecordService records(
        studentDao,
        [&database](const std::string& event, const std::string& username,
                    const std::string& rollNo, const std::string& outcome) {
            writeRecordAudit(database, event, username, rollNo, outcome);
        });
    srms::RecordController recordController(records, std::cin, std::cout);

    srms::ReportService reports(studentDao,
        [&database](const std::string& event, const std::string& outcome) {
            writeAudit(database, event, outcome);
        });

    srms::ConsoleUI ui(auth, std::cin, std::cout);
    ui.setRecordController(&recordController);
    ui.setReportsMenu([&reports](srms::Session& session) {
        runReports(session, reports);
    });

    ui.run();
    return 0;
}

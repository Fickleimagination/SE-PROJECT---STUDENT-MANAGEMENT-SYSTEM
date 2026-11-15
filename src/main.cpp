// Entry point: wires the services into the console menu.
#include <ctime>
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
    srms::RecordService records(studentDao);
    srms::RecordController recordController(records, std::cin, std::cout);

    // The audit hook must not be empty, or export would crash. No audit_log writer exists yet,
    // so this hook does nothing for now.
    srms::ReportService reports(studentDao,
        [](const std::string&, const std::string&) {});

    srms::ConsoleUI ui(auth, std::cin, std::cout);
    ui.setRecordController(&recordController);
    ui.setReportsMenu([&reports](srms::Session& session) {
        runReports(session, reports);
    });

    ui.run();
    return 0;
}

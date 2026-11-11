#include "record_controller.h"

#include <ctime>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>

namespace srms {
namespace {

constexpr int kMaxAttempts = 3;  // SRS UC-03 alternate flow 3a

std::string trimmed(const std::string& text) {
    const char* spaces = " \t\r\n";
    std::size_t first = text.find_first_not_of(spaces);
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(spaces) - first + 1);
}

}  // namespace

RecordController::RecordController(RecordService& service, std::istream& input, std::ostream& output,
                                   Validator validator)
    : service_(service), input_(input), output_(output), validator_(std::move(validator)) {}

bool RecordController::readLine(Session& session, const std::string& prompt, std::string& line) {
    output_ << prompt;
    if (!std::getline(input_, line)) return false;
    session.last_activity = std::time(nullptr);  // any input counts as activity
    return true;
}

// Asks for one field. Wrong input shows "ERROR: <field>: <rule>." and asks again, at most
// three times, then the whole operation is cancelled (UC-03 3a). With allowKeep, an empty
// answer keeps the stored value (REQ-CRUD-04).
RecordController::Answer RecordController::askField(Session& session, const std::string& field,
                                                    const std::string& prompt, bool allowKeep,
                                                    std::string& value) {
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        std::string raw;
        if (!readLine(session, prompt, raw)) return Answer::Cancelled;
        raw = trimmed(raw);
        if (allowKeep && raw.empty()) return Answer::Keep;
        auto problem = validator_.validateField(field, raw);
        if (!problem) {
            value = raw;
            return Answer::Value;
        }
        output_ << "ERROR: " << problem->field << ": " << problem->rule << ".\n";
    }
    output_ << "Operation cancelled.\n";
    return Answer::Cancelled;
}

void RecordController::showRecord(const Student& s) {
    output_ << "  roll_no:    " << s.roll_no << "\n"
            << "  name:       " << s.name << "\n"
            << "  department: " << s.department << "\n"
            << "  semester:   " << s.semester << "\n"
            << "  contact:    " << (s.contact.empty() ? "-" : s.contact) << "\n"
            << "  cgpa:       " << std::fixed << std::setprecision(2) << s.cgpa << "\n";
}

// One place that turns a Status into the SAD 4.4.2 error text.
void RecordController::report(Status status, const std::string& operation, const std::string& roll_no) {
    switch (status) {
        case Status::ACCESS_DENIED:
            output_ << "ERROR: access: administrator login required.\n";
            break;
        case Status::DUPLICATE_KEY:
            output_ << "ERROR: roll_no: " << roll_no << " already exists.\n";
            break;
        case Status::NOT_FOUND:
            output_ << "ERROR: roll_no: record not found.\n";
            break;
        case Status::VALIDATION_ERROR:
            output_ << "ERROR: input: a value is not valid.\n";
            break;
        case Status::DB_ERROR:
            if (operation == "add")
                output_ << "ERROR: database: record not saved.\n";
            else
                output_ << "ERROR: database: " << operation << " failed.\n";
            break;
        default:
            output_ << "ERROR: internal: " << operation << " failed.\n";
            break;
    }
}

void RecordController::addRecord(Session& session) {
    Student s{"", "", "", 0, "", 0.0, "", ""};
    std::string value;

    if (askField(session, "roll_no", "Roll number: ", false, value) != Answer::Value) return;
    s.roll_no = value;
    if (askField(session, "name", "Name: ", false, value) != Answer::Value) return;
    s.name = value;
    if (askField(session, "department", "Department (CSE, ECE, EEE, ME, CV, BT, AIML): ", false, value) !=
        Answer::Value)
        return;
    s.department = value;
    if (askField(session, "semester", "Semester (1-8): ", false, value) != Answer::Value) return;
    s.semester = std::stoi(value);
    if (askField(session, "contact", "Contact (optional, press Enter to skip): ", false, value) !=
        Answer::Value)
        return;
    s.contact = value;
    if (askField(session, "cgpa", "CGPA (0.00-10.00): ", false, value) != Answer::Value) return;
    s.cgpa = std::stod(value);

    Status status = service_.addStudent(session, s);
    if (status == Status::OK)
        output_ << "Record " << validator_.normalize(s).roll_no << " added successfully.\n";
    else
        report(status, "add", validator_.normalize(s).roll_no);
}

void RecordController::updateRecord(Session& session) {
    std::string roll_no;
    if (!readLine(session, "Roll number to update: ", roll_no)) return;
    roll_no = trimmed(roll_no);

    Result<Student> found = service_.getStudent(session, roll_no);
    if (found.status != Status::OK) {
        report(found.status, "update", roll_no);
        return;
    }
    const Student& current = found.value;
    output_ << "Current record (press Enter on any prompt to keep its value):\n";
    showRecord(current);

    StudentPatch patch;
    std::string value;
    Answer a = askField(session, "name", "Name [" + current.name + "]: ", true, value);
    if (a == Answer::Cancelled) return;
    if (a == Answer::Value) patch.name = value;

    a = askField(session, "department", "Department [" + current.department + "]: ", true, value);
    if (a == Answer::Cancelled) return;
    if (a == Answer::Value) patch.department = value;

    a = askField(session, "semester", "Semester [" + std::to_string(current.semester) + "]: ", true, value);
    if (a == Answer::Cancelled) return;
    if (a == Answer::Value) patch.semester = std::stoi(value);

    a = askField(session, "contact", "Contact [" + (current.contact.empty() ? std::string("-") : current.contact) + "]: ",
                 true, value);
    if (a == Answer::Cancelled) return;
    if (a == Answer::Value) patch.contact = value;

    std::ostringstream cgpa;
    cgpa << std::fixed << std::setprecision(2) << current.cgpa;
    a = askField(session, "cgpa", "CGPA [" + cgpa.str() + "]: ", true, value);
    if (a == Answer::Cancelled) return;
    if (a == Answer::Value) patch.cgpa = std::stod(value);

    Status status = service_.updateStudent(session, current.roll_no, patch);
    if (status == Status::OK)
        output_ << "Record " << current.roll_no << " updated successfully.\n";
    else
        report(status, "update", current.roll_no);
}

void RecordController::deleteRecord(Session& session) {
    std::string roll_no;
    if (!readLine(session, "Roll number to delete: ", roll_no)) return;
    roll_no = trimmed(roll_no);

    Result<Student> found = service_.getStudent(session, roll_no);
    if (found.status != Status::OK) {
        report(found.status, "delete", roll_no);
        return;
    }
    showRecord(found.value);

    std::string answer;
    if (!readLine(session, "Type YES to confirm deletion of " + found.value.roll_no + ": ", answer)) return;
    if (!RecordService::isDeleteConfirmed(answer)) {  // exactly "YES", nothing else (REQ-CRUD-05)
        output_ << "Deletion cancelled.\n";
        return;
    }
    Status status = service_.deleteStudent(session, found.value.roll_no);
    if (status == Status::OK)
        output_ << "Record " << found.value.roll_no << " deleted.\n";
    else
        report(status, "delete", found.value.roll_no);
}

void RecordController::listRecords(Session& session) {
    int page = 1;
    while (true) {
        Result<Page<Student>> result = service_.listStudents(session, page);
        if (result.status != Status::OK) {
            report(result.status, "list", "");
            return;
        }
        const Page<Student>& p = result.value;
        if (p.items.empty()) {
            output_ << "No records available.\n";
            return;
        }
        output_ << "\nPage " << p.page << " of " << p.total_pages << " (" << p.total_items << " records)\n"
                << std::left << std::setw(16) << "Roll no" << std::setw(28) << "Name" << std::setw(7)
                << "Dept" << std::setw(5) << "Sem" << "CGPA\n";
        for (const Student& s : p.items) {
            output_ << std::left << std::setw(16) << s.roll_no << std::setw(28) << s.name.substr(0, 26)
                    << std::setw(7) << s.department << std::setw(5) << s.semester << std::fixed
                    << std::setprecision(2) << s.cgpa << "\n";
        }
        if (p.total_pages == 1) return;

        std::string choice;
        if (!readLine(session, "[N]ext, [P]revious, [Q]uit: ", choice)) return;
        choice = trimmed(choice);
        if (choice == "N" || choice == "n") {
            if (page < p.total_pages) ++page; else output_ << "Already on the last page.\n";
        } else if (choice == "P" || choice == "p") {
            if (page > 1) --page; else output_ << "Already on the first page.\n";
        } else {
            return;
        }
    }
}

void RecordController::run(Session& session) {
    while (input_) {
        output_ << "\n===== Student Records =====\n"
                << "1. Add record\n"
                << "2. Update record\n"
                << "3. Delete record\n"
                << "4. List all records\n"
                << "5. Back\n";
        std::string choice;
        if (!readLine(session, "Enter your choice: ", choice)) return;
        choice = trimmed(choice);

        if (choice == "1") addRecord(session);
        else if (choice == "2") updateRecord(session);
        else if (choice == "3") deleteRecord(session);
        else if (choice == "4") listRecords(session);
        else if (choice == "5") return;
        else output_ << "Invalid choice. Enter 1, 2, 3, 4, or 5.\n";
    }
}

}  // namespace srms

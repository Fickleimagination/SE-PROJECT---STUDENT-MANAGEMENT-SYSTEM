// Field and record validation (owner: Bhavyaa Garg). SRS Appendix B.1, REQ-DATA-02..05, SAD 3.4.
// Console-free: pure functions, no <iostream>, printf or scanf (NFR-MNT-01).
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "types.h"

namespace srms {

// "field" is the lower_snake_case name; "rule" completes the console message
// "ERROR: <field>: <rule>." (SAD 4.4.2). Only ConsoleUI turns this into text.
struct ValidationError {
    std::string field;
    std::string rule;
};

class Validator {
public:
    // Default list from REQ-DATA-05; pass another list to change the allowed codes.
    explicit Validator(std::vector<std::string> departments = defaultDepartments());

    static std::vector<std::string> defaultDepartments();

    // Checks ONE value as the user typed it (raw text). Returns nothing when valid.
    // field is one of: roll_no, name, department, semester, contact, cgpa.
    std::optional<ValidationError> validateField(const std::string& field,
                                                 const std::string& raw) const;

    // Checks a whole record (defence in depth, SAD 4.2.2). Call normalize() first.
    std::optional<ValidationError> validateStudent(const Student& student) const;

    // Appendix B clean-up: roll_no trimmed and upper-cased, name/department/contact trimmed.
    Student normalize(Student student) const;

private:
    std::vector<std::string> departments_;
};

}  // namespace srms

#include "validator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <utility>

namespace srms {
namespace {

constexpr int kNameMaxChars = 80;     // Appendix B: name TEXT (1-80)
constexpr int kContactMaxChars = 50;  // Appendix B: contact TEXT (<=50)

// Typographic apostrophe (U+2019) as UTF-8. Appendix B writes it this way, so accept it too.
const char kCurlyApostrophe[] = "\xE2\x80\x99";

std::string trim(const std::string& text) {
    const char* spaces = " \t\r\n";
    std::size_t first = text.find_first_not_of(spaces);
    if (first == std::string::npos) return "";
    std::size_t last = text.find_last_not_of(spaces);
    return text.substr(first, last - first + 1);
}

std::string toUpper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

// Number of characters in a name, or -1 if it holds a character that is not allowed
// (allowed: letters, space, period, hyphen, apostrophe).
int nameLength(const std::string& name) {
    int count = 0;
    for (std::size_t i = 0; i < name.size();) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (std::isalpha(c) || c == ' ' || c == '.' || c == '-' || c == '\'') {
            i += 1;
        } else if (name.compare(i, 3, kCurlyApostrophe) == 0) {
            i += 3;  // three bytes, but one character
        } else {
            return -1;
        }
        ++count;
    }
    return count;
}

bool validCgpaNumber(double value) {
    if (!(value >= 0.0 && value <= 10.0)) return false;  // also rejects NaN
    double hundredths = value * 100.0;
    return std::fabs(hundredths - std::round(hundredths)) < 1e-6;  // at most 2 decimals
}

ValidationError err(const std::string& field, const std::string& rule) {
    return ValidationError{field, rule};
}

}  // namespace

Validator::Validator(std::vector<std::string> departments) : departments_(std::move(departments)) {}

std::vector<std::string> Validator::defaultDepartments() {
    return {"CSE", "ECE", "EEE", "ME", "CV", "BT", "AIML"};
}

std::optional<ValidationError> Validator::validateField(const std::string& field,
                                                        const std::string& raw) const {
    const std::string value = trim(raw);

    if (field == "roll_no") {
        static const std::regex pattern("^[A-Z0-9]{5,20}$");
        if (!std::regex_match(toUpper(value), pattern))
            return err(field, "must be 5-20 letters or digits");
        return std::nullopt;
    }

    if (field == "name") {
        int length = nameLength(value);
        if (value.empty() || length < 0 || length > kNameMaxChars)
            return err(field, "must be 1-80 characters: letters, spaces, . - ' only");
        return std::nullopt;
    }

    if (field == "department") {
        if (std::find(departments_.begin(), departments_.end(), value) == departments_.end()) {
            std::string list;
            for (std::size_t i = 0; i < departments_.size(); ++i)
                list += (i ? ", " : "") + departments_[i];
            return err(field, "must be one of " + list);
        }
        return std::nullopt;
    }

    if (field == "semester") {
        static const std::regex pattern("^[1-8]$");
        if (!std::regex_match(value, pattern)) return err(field, "must be a whole number from 1 to 8");
        return std::nullopt;
    }

    if (field == "contact") {
        if (value.empty()) return std::nullopt;  // optional field
        static const std::regex phone("^[6-9][0-9]{9}$");
        static const std::regex email("^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$");
        bool shapeOk = std::regex_match(value, phone) || std::regex_match(value, email);
        if (!shapeOk || value.size() > static_cast<std::size_t>(kContactMaxChars))
            return err(field, "must be a 10-digit number starting 6-9 or an e-mail (max 50 characters)");
        return std::nullopt;
    }

    if (field == "cgpa") {
        static const std::regex pattern("^(10(\\.0{1,2})?|[0-9](\\.[0-9]{1,2})?)$");
        if (!std::regex_match(value, pattern))
            return err(field, "must be between 0.00 and 10.00 with at most 2 decimal places");
        return std::nullopt;
    }

    return err(field, "is not a known field");
}

std::optional<ValidationError> Validator::validateStudent(const Student& s) const {
    // Same order the console prompts for them (SRS UC-03 step 2).
    if (auto e = validateField("roll_no", s.roll_no)) return e;
    if (auto e = validateField("name", s.name)) return e;
    if (auto e = validateField("department", s.department)) return e;
    if (auto e = validateField("semester", std::to_string(s.semester))) return e;
    if (auto e = validateField("contact", s.contact)) return e;
    if (!validCgpaNumber(s.cgpa))
        return err("cgpa", "must be between 0.00 and 10.00 with at most 2 decimal places");
    return std::nullopt;
}

Student Validator::normalize(Student s) const {
    s.roll_no = toUpper(trim(s.roll_no));
    s.name = trim(s.name);
    s.department = trim(s.department);
    s.contact = trim(s.contact);
    return s;
}

}  // namespace srms

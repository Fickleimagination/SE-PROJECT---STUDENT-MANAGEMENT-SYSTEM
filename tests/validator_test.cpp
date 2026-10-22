// GoogleTest suite for Validator (SRS Appendix B.1; REQ-DATA-02..05; test plan TC-DATA-02).
#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "../src/core/validator.h"

using namespace srms;

namespace {

Validator validator;

bool ok(const std::string& field, const std::string& value) {
    return !validator.validateField(field, value).has_value();
}

Student goodStudent() {
    return Student{"PES1UG24CS116", "Bhavyaa Garg", "CSE", 5, "9876543210", 8.41, "", ""};
}

}  // namespace

// REQ-DATA-02: roll_no ^[A-Z0-9]{5,20}$ after upper-casing.
TEST(ValidatorRollNo, AcceptsAndRejectsPerFitCriterion) {
    EXPECT_TRUE(ok("roll_no", "PES1UG24CS116"));
    EXPECT_TRUE(ok("roll_no", "pes1ug24cs116"));  // upper-cased before the check
    EXPECT_TRUE(ok("roll_no", "AB123"));          // 5 characters: lower boundary
    EXPECT_TRUE(ok("roll_no", std::string(20, 'A')));
    EXPECT_FALSE(ok("roll_no", "AB1"));
    EXPECT_FALSE(ok("roll_no", "PES-116"));
    EXPECT_FALSE(ok("roll_no", ""));
    EXPECT_FALSE(ok("roll_no", std::string(21, 'A')));
}

// REQ-DATA-03: name 1-80 chars; letters, space, period, hyphen, apostrophe.
TEST(ValidatorName, AcceptsAndRejectsPerFitCriterion) {
    EXPECT_TRUE(ok("name", "A"));
    EXPECT_TRUE(ok("name", "Anne-Marie O\xE2\x80\x99Neil"));  // curly apostrophe, as in the SRS
    EXPECT_TRUE(ok("name", "Anne-Marie O'Neil"));
    EXPECT_TRUE(ok("name", "Dr. A. P. J."));
    EXPECT_TRUE(ok("name", std::string(80, 'a')));
    EXPECT_FALSE(ok("name", ""));
    EXPECT_FALSE(ok("name", "   "));
    EXPECT_FALSE(ok("name", std::string(81, 'a')));
    EXPECT_FALSE(ok("name", "R2D2"));
    EXPECT_FALSE(ok("name", "Bob; DROP TABLE students;--"));
}

// The 80-character limit counts characters, not bytes (a curly apostrophe is 3 bytes).
TEST(ValidatorName, CountsCharactersNotBytes) {
    std::string name = std::string(79, 'a') + "\xE2\x80\x99";  // 80 characters, 82 bytes
    EXPECT_TRUE(ok("name", name));
}

// REQ-DATA-04: semester integer 1-8.
TEST(ValidatorSemester, AcceptsAndRejectsPerFitCriterion) {
    EXPECT_TRUE(ok("semester", "1"));
    EXPECT_TRUE(ok("semester", "8"));
    EXPECT_FALSE(ok("semester", "0"));
    EXPECT_FALSE(ok("semester", "9"));
    EXPECT_FALSE(ok("semester", "2.5"));
    EXPECT_FALSE(ok("semester", "abc"));
    EXPECT_FALSE(ok("semester", ""));
}

// REQ-DATA-04: cgpa 0.00-10.00, at most 2 decimal places.
TEST(ValidatorCgpa, AcceptsAndRejectsPerFitCriterion) {
    EXPECT_TRUE(ok("cgpa", "0.00"));
    EXPECT_TRUE(ok("cgpa", "10.00"));
    EXPECT_TRUE(ok("cgpa", "8.41"));
    EXPECT_TRUE(ok("cgpa", "7"));
    EXPECT_FALSE(ok("cgpa", "-0.01"));
    EXPECT_FALSE(ok("cgpa", "10.01"));
    EXPECT_FALSE(ok("cgpa", "8.123"));
    EXPECT_FALSE(ok("cgpa", "abc"));
    EXPECT_FALSE(ok("cgpa", ""));
}

// REQ-DATA-05: department must be one of the stored codes; the error lists them.
TEST(ValidatorDepartment, AcceptsListedCodesAndShowsListOnFailure) {
    EXPECT_TRUE(ok("department", "CSE"));
    EXPECT_TRUE(ok("department", "AIML"));
    auto e = validator.validateField("department", "XYZ");
    ASSERT_TRUE(e.has_value());
    EXPECT_NE(e->rule.find("CSE, ECE, EEE, ME, CV, BT, AIML"), std::string::npos);
}

TEST(ValidatorDepartment, UsesCustomCodeList) {
    Validator custom({"PHY", "CHE"});
    EXPECT_FALSE(custom.validateField("department", "PHY").has_value());
    EXPECT_TRUE(custom.validateField("department", "CSE").has_value());
}

// Appendix B: contact is optional; otherwise a 10-digit number starting 6-9 or an e-mail.
TEST(ValidatorContact, OptionalPhoneOrEmail) {
    EXPECT_TRUE(ok("contact", ""));
    EXPECT_TRUE(ok("contact", "9876543210"));
    EXPECT_TRUE(ok("contact", "a.b@pes.edu"));
    EXPECT_FALSE(ok("contact", "1234567890"));   // must start 6-9
    EXPECT_FALSE(ok("contact", "98765"));
    EXPECT_FALSE(ok("contact", "not-an-email"));
    EXPECT_FALSE(ok("contact", "a b@pes.edu"));
    EXPECT_FALSE(ok("contact", std::string(45, 'a') + "@b.com"));  // over 50 characters
}

TEST(ValidatorField, UnknownFieldIsRejected) {
    auto e = validator.validateField("shoe_size", "9");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->field, "shoe_size");
}

TEST(ValidatorField, ErrorNamesTheFailingField) {
    auto e = validator.validateField("semester", "9");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->field, "semester");
}

// Whole-record check.
TEST(ValidatorStudent, AcceptsGoodRecord) {
    EXPECT_FALSE(validator.validateStudent(goodStudent()).has_value());
}

TEST(ValidatorStudent, ReportsFirstBadFieldInPromptOrder) {
    Student s = goodStudent();
    s.semester = 9;
    s.cgpa = 11.0;  // also bad, but semester comes first
    auto e = validator.validateStudent(s);
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->field, "semester");
}

TEST(ValidatorStudent, RejectsCgpaOutOfRangeOrTooPrecise) {
    for (double bad : {-0.01, 10.01, 8.123}) {
        Student s = goodStudent();
        s.cgpa = bad;
        auto e = validator.validateStudent(s);
        ASSERT_TRUE(e.has_value()) << bad;
        EXPECT_EQ(e->field, "cgpa");
    }
    for (double good : {0.0, 10.0, 8.41, 7.5}) {
        Student s = goodStudent();
        s.cgpa = good;
        EXPECT_FALSE(validator.validateStudent(s).has_value()) << good;
    }
}

TEST(ValidatorStudent, RejectsNaNCgpa) {
    Student s = goodStudent();
    s.cgpa = std::nan("");
    EXPECT_TRUE(validator.validateStudent(s).has_value());
}

// Appendix B clean-up.
TEST(ValidatorNormalize, UppercasesRollNoAndTrimsText) {
    Student s = goodStudent();
    s.roll_no = "  pes1ug24cs116 ";
    s.name = "  Bhavyaa Garg  ";
    Student n = validator.normalize(s);
    EXPECT_EQ(n.roll_no, "PES1UG24CS116");
    EXPECT_EQ(n.name, "Bhavyaa Garg");
}

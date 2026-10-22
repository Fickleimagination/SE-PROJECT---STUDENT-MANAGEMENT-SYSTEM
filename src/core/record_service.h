// Student record CRUD (owner: Bhavyaa Garg). SRS 5.2, SAD 4.2.2 and 4.3.2.
// This file covers US-03 (Add). Update, Delete and List are added in later PRs.
// Console-free: no <iostream>, printf or scanf here (NFR-MNT-01).
#pragma once
#include <ctime>
#include <functional>
#include <string>

#include "types.h"
#include "validator.h"

namespace srms {

// Storage the service talks to. Implemented by StudentDAO (parameterized SQL only, SEC-02);
// tests use an in-memory fake, exactly like ReportService's StudentSource.
class StudentRepository {
public:
    virtual ~StudentRepository() = default;

    // OK + value true/false, or DB_ERROR.
    virtual Result<bool> exists(const std::string& roll_no) = 0;

    // Inserts one row inside one transaction (BEGIN IMMEDIATE ... COMMIT, SAD 4.4.4).
    // Returns OK, DUPLICATE_KEY (primary-key clash) or DB_ERROR (after ROLLBACK).
    virtual Status insert(const Student& student) = 0;
};

// Called after a successful change so audit_log gets a row (SEC-04):
// event e.g. "RECORD_ADDED", then the acting username, the target roll_no, and "SUCCESS".
using RecordAuditHook = std::function<void(const std::string& event, const std::string& username,
                                           const std::string& roll_no, const std::string& outcome)>;

// Source of "now"; tests pass a fixed one so timestamps are predictable.
using Clock = std::function<std::time_t()>;

class RecordService {
public:
    // audit and clock may be empty: no audit rows, and the real system clock.
    RecordService(StudentRepository& repo, RecordAuditHook audit = nullptr,
                  Validator validator = Validator(), Clock clock = nullptr);

    // UC-03. OK; ACCESS_DENIED (no active admin session, SEC-07);
    // VALIDATION_ERROR (any Appendix B rule broken); DUPLICATE_KEY (roll_no exists, REQ-CRUD-02);
    // DB_ERROR (storage failed, nothing saved); INTERNAL_ERROR (unexpected exception).
    // created_at and updated_at are set here, never taken from the caller (REQ-CRUD-08).
    Status addStudent(const Session& session, const Student& student);

private:
    bool isActiveAdmin(const Session& session, std::time_t now) const;

    StudentRepository& repo_;
    RecordAuditHook audit_;
    Validator validator_;
    Clock clock_;
};

}  // namespace srms

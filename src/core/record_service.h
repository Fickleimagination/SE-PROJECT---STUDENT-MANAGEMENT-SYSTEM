// Student record CRUD (owner: Bhavyaa Garg). SRS 5.2, SAD 4.2.2 and 4.3.2.
// US-03 Add, US-04 Update, US-05 Delete, US-06 List.
// Console-free: no <iostream>, printf or scanf here (NFR-MNT-01).
#pragma once
#include <ctime>
#include <functional>
#include <string>
#include <vector>

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

    // The stored record, or NOT_FOUND / DB_ERROR.
    virtual Result<Student> find(const std::string& roll_no) = 0;

    // Overwrites the row for student.roll_no in one transaction. OK, NOT_FOUND or DB_ERROR.
    virtual Status update(const Student& student) = 0;

    // Deletes one row in one transaction. OK, NOT_FOUND or DB_ERROR.
    virtual Status remove(const std::string& roll_no) = 0;

    // Total number of stored records.
    virtual Result<int> count() = 0;

    // Up to `limit` records ordered by roll_no ascending, skipping the first `offset`.
    virtual Result<std::vector<Student>> fetch(int offset, int limit) = 0;
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

    // One record by roll_no. ACCESS_DENIED, NOT_FOUND (REQ-CRUD-07), DB_ERROR.
    Result<Student> getStudent(const Session& session, const std::string& roll_no);

    // UC-04. Only the fields set in the patch change (REQ-CRUD-03, 04). The merged record is
    // validated as a whole, updated_at is refreshed, created_at and roll_no never change.
    // OK; ACCESS_DENIED; NOT_FOUND; VALIDATION_ERROR; DB_ERROR; INTERNAL_ERROR.
    Status updateStudent(const Session& session, const std::string& roll_no, const StudentPatch& patch);

    // UC-05. The caller must already have the administrator's confirmation (isDeleteConfirmed).
    // OK; ACCESS_DENIED; NOT_FOUND; DB_ERROR; INTERNAL_ERROR.
    Status deleteStudent(const Session& session, const std::string& roll_no);

    // REQ-CRUD-05 / NFR-SAFE-01: true only for exactly "YES" (case-sensitive, no extra spaces).
    static bool isDeleteConfirmed(const std::string& typed);

    // UC-06 / REQ-CRUD-06. Page numbers start at 1, 20 records per page, ordered by roll_no.
    // An empty database is OK with an empty page 1. A page past the end is VALIDATION_ERROR.
    static constexpr int kPageSize = 20;
    Result<Page<Student>> listStudents(const Session& session, int page);

private:
    bool isActiveAdmin(const Session& session, std::time_t now) const;

    StudentRepository& repo_;
    RecordAuditHook audit_;
    Validator validator_;
    Clock clock_;
};

}  // namespace srms

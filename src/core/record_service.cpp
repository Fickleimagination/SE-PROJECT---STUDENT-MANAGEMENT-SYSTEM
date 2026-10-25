#include "record_service.h"

#include <cstdio>
#include <utility>

namespace srms {
namespace {

constexpr std::time_t kIdleLimitSeconds = 600;  // REQ-AUTH-08: session ends after 10 idle minutes

// "YYYY-MM-DD HH:MM:SS" in local time (Appendix B, REQ-CRUD-08).
std::string timestamp(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

}  // namespace

RecordService::RecordService(StudentRepository& repo, RecordAuditHook audit, Validator validator,
                             Clock clock)
    : repo_(repo),
      audit_(std::move(audit)),
      validator_(std::move(validator)),
      clock_(std::move(clock)) {
    if (!clock_) clock_ = [] { return std::time(nullptr); };
}

// SEC-07: the check lives here, inside the service, not only in the menu.
bool RecordService::isActiveAdmin(const Session& session, std::time_t now) const {
    return session.is_admin && (now - session.last_activity) <= kIdleLimitSeconds;
}

Status RecordService::addStudent(const Session& session, const Student& student) {
    try {
        const std::time_t now = clock_();

        // 1. Only an active administrator may add (REQ-AUTH-06, SEC-07).
        if (!isActiveAdmin(session, now)) return Status::ACCESS_DENIED;

        // 2. Clean up, then re-check every rule even though ConsoleUI already did (SAD 4.2.2).
        Student record = validator_.normalize(student);
        if (validator_.validateStudent(record)) return Status::VALIDATION_ERROR;

        // 3. roll_no must be new (REQ-CRUD-02).
        Result<bool> found = repo_.exists(record.roll_no);
        if (found.status != Status::OK) return found.status;  // DB_ERROR
        if (found.value) return Status::DUPLICATE_KEY;

        // 4. The system sets both timestamps; the caller's values are ignored (REQ-CRUD-08).
        record.created_at = timestamp(now);
        record.updated_at = record.created_at;

        // 5. One transaction. The repository also guards against a duplicate (primary key).
        Status saved = repo_.insert(record);
        if (saved != Status::OK) return saved;

        // 6. Only successful changes are audited (SEC-04).
        if (audit_) audit_("RECORD_ADDED", session.username, record.roll_no, "SUCCESS");
        return Status::OK;
    } catch (...) {
        return Status::INTERNAL_ERROR;  // service boundary: nothing escapes as an exception (SAD 4.4.1)
    }
}

Result<Student> RecordService::getStudent(const Session& session, const std::string& roll_no) {
    try {
        if (!isActiveAdmin(session, clock_())) return {Status::ACCESS_DENIED, Student{}, ""};
        return repo_.find(validator_.normalize(Student{roll_no, "", "", 0, "", 0.0, "", ""}).roll_no);
    } catch (...) {
        return {Status::INTERNAL_ERROR, Student{}, ""};
    }
}

Status RecordService::updateStudent(const Session& session, const std::string& roll_no,
                                    const StudentPatch& patch) {
    try {
        const std::time_t now = clock_();
        if (!isActiveAdmin(session, now)) return Status::ACCESS_DENIED;

        const std::string key = validator_.normalize(Student{roll_no, "", "", 0, "", 0.0, "", ""}).roll_no;
        Result<Student> current = repo_.find(key);
        if (current.status != Status::OK) return current.status;  // NOT_FOUND or DB_ERROR

        // Merge: a field that is not in the patch keeps its stored value (REQ-CRUD-04).
        Student merged = current.value;
        if (patch.name) merged.name = *patch.name;
        if (patch.department) merged.department = *patch.department;
        if (patch.contact) merged.contact = *patch.contact;
        if (patch.semester) merged.semester = *patch.semester;
        if (patch.cgpa) merged.cgpa = *patch.cgpa;

        merged = validator_.normalize(merged);
        if (validator_.validateStudent(merged)) return Status::VALIDATION_ERROR;

        merged.roll_no = current.value.roll_no;        // immutable (BR-02)
        merged.created_at = current.value.created_at;  // never changes
        merged.updated_at = timestamp(now);

        Status saved = repo_.update(merged);
        if (saved != Status::OK) return saved;

        if (audit_) audit_("RECORD_UPDATED", session.username, merged.roll_no, "SUCCESS");
        return Status::OK;
    } catch (...) {
        return Status::INTERNAL_ERROR;
    }
}

Status RecordService::deleteStudent(const Session& session, const std::string& roll_no) {
    try {
        if (!isActiveAdmin(session, clock_())) return Status::ACCESS_DENIED;

        const std::string key = validator_.normalize(Student{roll_no, "", "", 0, "", 0.0, "", ""}).roll_no;
        Result<bool> found = repo_.exists(key);
        if (found.status != Status::OK) return found.status;
        if (!found.value) return Status::NOT_FOUND;  // REQ-CRUD-07

        Status removed = repo_.remove(key);
        if (removed != Status::OK) return removed;

        if (audit_) audit_("RECORD_DELETED", session.username, key, "SUCCESS");
        return Status::OK;
    } catch (...) {
        return Status::INTERNAL_ERROR;
    }
}

bool RecordService::isDeleteConfirmed(const std::string& typed) { return typed == "YES"; }

Result<Page<Student>> RecordService::listStudents(const Session& session, int page) {
    try {
        if (!isActiveAdmin(session, clock_())) return {Status::ACCESS_DENIED, Page<Student>{}, ""};

        Result<int> total = repo_.count();
        if (total.status != Status::OK) return {total.status, Page<Student>{}, ""};

        const int totalPages = total.value == 0 ? 1 : (total.value + kPageSize - 1) / kPageSize;
        if (page < 1 || page > totalPages) return {Status::VALIDATION_ERROR, Page<Student>{}, ""};

        Result<std::vector<Student>> rows = repo_.fetch((page - 1) * kPageSize, kPageSize);
        if (rows.status != Status::OK) return {rows.status, Page<Student>{}, ""};

        return {Status::OK, Page<Student>{rows.value, page, totalPages, total.value}, ""};
    } catch (...) {
        return {Status::INTERNAL_ERROR, Page<Student>{}, ""};
    }
}

}  // namespace srms

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

}  // namespace srms

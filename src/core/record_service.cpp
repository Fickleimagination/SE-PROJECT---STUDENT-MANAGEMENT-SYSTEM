#include "record_service.h"

#include <ctime>
#include <utility>

namespace srms {
namespace {

constexpr std::time_t kIdleLimitSeconds = 600;

// Format timestamp as "YYYY-MM-DD HH:MM:SS".
std::string timestamp(std::time_t t) {
    std::tm tm{};

#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

} // namespace

RecordService::RecordService(
    StudentRepository& repo,
    RecordAuditHook audit,
    Validator validator,
    Clock clock)
    : repo_(repo),
      audit_(std::move(audit)),
      validator_(std::move(validator)),
      clock_(std::move(clock)) {

    if (!clock_) {
        clock_ = [] { return std::time(nullptr); };
    }
}

// Security check: only an administrator with a valid, non-expired
// session may perform administrator operations.
bool RecordService::isActiveAdmin(
    const Session& session,
    std::time_t now) const {

    return session.is_admin &&
           !session.username.empty() &&
           now >= session.last_activity &&
           now - session.last_activity <= kIdleLimitSeconds;
}

Status RecordService::addStudent(
    const Session& session,
    const Student& student) {

    try {
        const std::time_t now = clock_();

        // Only an active administrator may add a student.
        if (!isActiveAdmin(session, now)) {
            return Status::ACCESS_DENIED;
        }

        // Normalize and validate every field.
        Student record = validator_.normalize(student);

        if (validator_.validateStudent(record)) {
            return Status::VALIDATION_ERROR;
        }

        // Check whether the roll number already exists.
        Result<bool> found = repo_.exists(record.roll_no);

        if (found.status != Status::OK) {
            return found.status;
        }

        if (found.value) {
            return Status::DUPLICATE_KEY;
        }

        // Generate timestamps inside the service.
        record.created_at = timestamp(now);
        record.updated_at = record.created_at;

        // Save the record.
        Status saved = repo_.insert(record);

        if (saved != Status::OK) {
            return saved;
        }

        // Audit only successful additions.
        if (audit_) {
            audit_(
                "RECORD_ADDED",
                session.username,
                record.roll_no,
                "SUCCESS");
        }

        return Status::OK;

    } catch (...) {
        return Status::INTERNAL_ERROR;
    }
}

} // namespace srms
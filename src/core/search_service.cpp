#include "search_service.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <utility>

namespace srms {
namespace {

constexpr std::time_t kIdleTimeoutSeconds = 600;

Page<Student> emptyPage() {
    return Page<Student>{{}, 1, 0, 0};
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");

    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

// Counts UTF-8 leading bytes rather than raw bytes.
std::size_t characterCount(const std::string& value) {
    std::size_t count = 0;

    for (unsigned char ch : value) {
        if ((ch & 0xC0U) != 0x80U) {
            ++count;
        }
    }

    return count;
}

std::string uppercaseRollNo(const std::string& value) {
    std::string result = value;

    for (char& ch : result) {
        const auto byte = static_cast<unsigned char>(ch);
        ch = static_cast<char>(std::toupper(byte));
    }

    return result;
}

bool isActiveSession(
    const Session& session,
    std::time_t now
) {
    return now >= session.last_activity &&
        now - session.last_activity <= kIdleTimeoutSeconds;
}

}  // namespace

SearchService::SearchService(
    SearchRepository& repository,
    SearchClock clock
)
    : repository_(repository),
      clock_(clock ? std::move(clock) : [] {
          return std::time(nullptr);
      }) {}

Result<Page<Student>> SearchService::search(
    const std::optional<Session>& session,
    const std::string& rawTerm
) {
    const Page<Student> empty = emptyPage();

    try {
        const std::string term = trim(rawTerm);

        if (term.empty()) {
            return {
                Status::VALIDATION_ERROR,
                empty,
                "search term is required"
            };
        }

        const std::time_t now = clock_();

        if (session.has_value() &&
            !isActiveSession(*session, now)) {
            return {
                Status::ACCESS_DENIED,
                empty,
                "session is expired"
            };
        }

        const bool isAdmin =
            session.has_value() && session->is_admin;

        if (isAdmin && characterCount(term) < 2) {
            return {
                Status::VALIDATION_ERROR,
                empty,
                "name search requires at least 2 characters"
            };
        }

        auto result = repository_.search(term, isAdmin);

        if (result.status != Status::OK) {
            return {
                result.status,
                empty,
                result.detail
            };
        }

        auto items = std::move(result.value);

        // Roll numbers are ASCII identifiers, so ASCII
        // case-insensitive ordering is sufficient here.
        std::sort(
            items.begin(),
            items.end(),
            [](const Student& left, const Student& right) {
                const std::string leftRoll =
                    uppercaseRollNo(left.roll_no);
                const std::string rightRoll =
                    uppercaseRollNo(right.roll_no);

                if (leftRoll != rightRoll) {
                    return leftRoll < rightRoll;
                }

                return left.roll_no < right.roll_no;
            }
        );

        if (!isAdmin) {
            for (Student& student : items) {
                student.contact.clear();
                student.created_at.clear();
                student.updated_at.clear();
            }
        }

        const int count = static_cast<int>(items.size());

        Page<Student> page{
            std::move(items),
            1,
            count == 0 ? 0 : 1,
            count
        };

        return {
            Status::OK,
            std::move(page),
            count == 0 ? "No matching record found." : ""
        };
    } catch (...) {
        return {
            Status::INTERNAL_ERROR,
            empty,
            "unexpected search error"
        };
    }
}

}  // namespace srms
#include "search_service.h"

#include "validator.h"
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

bool isActiveAdmin(
    const Session& session,
    std::time_t now
) {
    return session.is_admin &&
        !session.username.empty() &&
        isActiveSession(session, now);
}


Result<std::string> prepareSearchTerm(
    const std::string& rawTerm,
    const std::optional<Session>& session,
    std::time_t now,
    bool& isAdmin
) {
    std::string term = trim(rawTerm);

    if (term.empty()) {
        return {Status::VALIDATION_ERROR, {}, "search term is required"};
    }

    if (session && !isActiveSession(*session, now)) {
        return {Status::ACCESS_DENIED, {}, "session is expired"};
    }

    isAdmin = session && session->is_admin;

    if (isAdmin && characterCount(term) < 2) {
        return {
            Status::VALIDATION_ERROR,
            {},
            "name search requires at least 2 characters"
        };
    }

    return {Status::OK, std::move(term), ""};
}

bool rollNumberLess(const Student& left, const Student& right) {
    const std::string leftRoll = uppercaseRollNo(left.roll_no);
    const std::string rightRoll = uppercaseRollNo(right.roll_no);

    if (leftRoll != rightRoll) {
        return leftRoll < rightRoll;
    }

    return left.roll_no < right.roll_no;
}

void sortByRollNumber(std::vector<Student>& students) {
    std::sort(students.begin(), students.end(), rollNumberLess);
}

void redactViewerFields(std::vector<Student>& students) {
    for (Student& student : students) {
        student.contact.clear();
        student.created_at.clear();
        student.updated_at.clear();
    }
}

Result<Page<Student>> makePageResult(
    std::vector<Student> students,
    const char* emptyMessage
) {
    const int count = static_cast<int>(students.size());

    Page<Student> page{
        std::move(students),
        1,
        count == 0 ? 0 : 1,
        count
    };

    return {
        Status::OK,
        std::move(page),
        count == 0 ? emptyMessage : ""
    };
}

bool isValidSortField(Field field) {
    switch (field) {
        case Field::Department:
        case Field::Semester:
        case Field::Cgpa:
            return true;
        default:
            return false;
    }
}

bool isValidSortOrder(Order order) {
    return order == Order::Ascending ||
           order == Order::Descending;
}

Result<FilterCriteria> normalizeFilterCriteria(
    const FilterCriteria& criteria
) {
    if (!criteria.department && !criteria.semester) {
        return {
            Status::VALIDATION_ERROR,
            {},
            "at least one filter criterion is required"
        };
    }

    Validator validator;
    FilterCriteria normalized = criteria;

    if (normalized.department) {
        std::string department = trim(*normalized.department);

        for (char& ch : department) {
            ch = static_cast<char>(
                std::toupper(static_cast<unsigned char>(ch))
            );
        }

        const auto error =
            validator.validateField("department", department);

        if (error) {
            return {Status::VALIDATION_ERROR, {}, error->rule};
        }

        normalized.department = department;
    }

    if (normalized.semester) {
        const auto error = validator.validateField(
            "semester",
            std::to_string(*normalized.semester)
        );

        if (error) {
            return {Status::VALIDATION_ERROR, {}, error->rule};
        }
    }

    return {Status::OK, std::move(normalized), ""};
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
        bool isAdmin = false;
        auto request = prepareSearchTerm(
            rawTerm, session, clock_(), isAdmin
        );

        if (request.status != Status::OK) {
            return {request.status, empty, request.detail};
        }

        auto result = repository_.search(request.value, isAdmin);

        if (result.status != Status::OK) {
            return {result.status, empty, result.detail};
        }

        auto items = std::move(result.value);
        sortByRollNumber(items);

        if (!isAdmin) {
            redactViewerFields(items);
        }

        return makePageResult(
            std::move(items),
            "No matching record found."
        );
    } catch (...) {
        return {
            Status::INTERNAL_ERROR,
            empty,
            "unexpected search error"
        };
    }
}

// US-10: Sort the complete student list.
Result<Page<Student>> SearchService::sortRecords(
    const Session& session,
    Field field,
    Order order
) {
    const Page<Student> empty = emptyPage();

    try {
        if (!isActiveAdmin(session, clock_())) {
            return {
                Status::ACCESS_DENIED,
                empty,
                "active administrator session required"
            };
        }

        if (!isValidSortField(field)) {
            return {
                Status::VALIDATION_ERROR,
                empty,
                "invalid sort field"
            };
        }

        if (!isValidSortOrder(order)) {
            return {
                Status::VALIDATION_ERROR,
                empty,
                "invalid sort order"
            };
        }

        auto result = repository_.sortRecords(field, order);

        if (result.status != Status::OK) {
            return {result.status, empty, result.detail};
        }

        return makePageResult(
            std::move(result.value),
            "No records available."
        );
    } catch (...) {
        return {
            Status::INTERNAL_ERROR,
            empty,
            "unexpected sorting error"
        };
    }
}

// US-11: Filter by department, semester, or both.
Result<Page<Student>> SearchService::filterRecords(
    const Session& session,
    const FilterCriteria& criteria
) {
    const Page<Student> empty = emptyPage();

    try {
        if (!isActiveAdmin(session, clock_())) {
            return {
                Status::ACCESS_DENIED,
                empty,
                "active administrator session required"
            };
        }

        auto normalized = normalizeFilterCriteria(criteria);

        if (normalized.status != Status::OK) {
            return {normalized.status, empty, normalized.detail};
        }

        auto result = repository_.filterRecords(normalized.value);

        if (result.status != Status::OK) {
            return {result.status, empty, result.detail};
        }

        return makePageResult(
            std::move(result.value),
            "No records available."
        );
    } catch (...) {
        return {
            Status::INTERNAL_ERROR,
            empty,
            "unexpected filtering error"
        };
    }
}


}  // namespace srms
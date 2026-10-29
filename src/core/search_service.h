#pragma once

#include "types.h"

#include <ctime>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace srms {

// Implemented by the DAO. Contains no console I/O.
class SearchRepository {
public:
    virtual ~SearchRepository() = default;

    virtual Result<std::vector<Student>> search(
        const std::string& term,
        bool allow_name_search
    ) = 0;
};

using SearchClock = std::function<std::time_t()>;

class SearchService {
public:
    explicit SearchService(
        SearchRepository& repository,
        SearchClock clock = SearchClock{}
    );

    Result<Page<Student>> search(
        const std::optional<Session>& session,
        const std::string& term
    );

private:
    SearchRepository& repository_;
    SearchClock clock_;
};

}  // namespace srms
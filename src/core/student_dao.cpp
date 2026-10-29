#include "student_dao.h"

#include <sqlite3.h>

#include <string>
#include <utility>
#include <vector>

namespace srms {
namespace {

bool execute(sqlite3* db, const char* sql) {
    return sqlite3_exec(
        db, sql, nullptr, nullptr, nullptr
    ) == SQLITE_OK;
}

void rollback(sqlite3* db) noexcept {
    if (db != nullptr) {
        sqlite3_exec(
            db, "ROLLBACK;", nullptr, nullptr, nullptr
        );
    }
}

std::string columnText(sqlite3_stmt* statement, int column) {
    const unsigned char* value =
        sqlite3_column_text(statement, column);

    return value == nullptr
        ? std::string{}
        : std::string(
              reinterpret_cast<const char*>(value)
          );
}

int bindText(
    sqlite3_stmt* statement,
    int index,
    const std::string& value
) {
    return sqlite3_bind_text(
        statement,
        index,
        value.c_str(),
        static_cast<int>(value.size()),
        SQLITE_TRANSIENT
    );
}

// Escape SQL LIKE wildcard characters so that user input is
// searched literally. The backslash is the LIKE escape character.
std::string escapeLike(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (char ch : value) {
        if (ch == '%' || ch == '_' || ch == '\\') {
            escaped.push_back('\\');
        }

        escaped.push_back(ch);
    }

    return escaped;
}

}  // namespace

Result<bool> StudentDAO::exists(
    const std::string& roll_no
) {
    sqlite3* db = database_.handle();

    if (db == nullptr) {
        return {
            Status::DB_ERROR,
            false,
            "database is not open"
        };
    }

    const char* sql =
        "SELECT 1 FROM students "
        "WHERE roll_no = ?1 COLLATE NOCASE "
        "LIMIT 1;";

    sqlite3_stmt* statement = nullptr;

    int rc = sqlite3_prepare_v2(
        db, sql, -1, &statement, nullptr
    );

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return {
            Status::DB_ERROR,
            false,
            "database query failed"
        };
    }

    rc = bindText(statement, 1, roll_no);

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return {
            Status::DB_ERROR,
            false,
            "database query failed"
        };
    }

    rc = sqlite3_step(statement);

    const bool found = (rc == SQLITE_ROW);
    const bool query_ok =
        found || rc == SQLITE_DONE;

    const int finalize_rc =
        sqlite3_finalize(statement);

    if (!query_ok || finalize_rc != SQLITE_OK) {
        return {
            Status::DB_ERROR,
            false,
            "database query failed"
        };
    }

    return {Status::OK, found, ""};
}

// US-08: Search by exact roll number, with optional partial-name search.
Result<std::vector<Student>> StudentDAO::search(
    const std::string& term,
    bool allow_name_search
) {
    sqlite3* db = database_.handle();

    if (db == nullptr) {
        return {
            Status::DB_ERROR,
            {},
            "database is not open"
        };
    }

    const char* sql =
        "SELECT roll_no, name, department, semester, "
        "contact, cgpa, created_at, updated_at "
        "FROM students "
        "WHERE roll_no = ?1 COLLATE NOCASE "
        "OR (?2 = 1 AND name LIKE ?3 ESCAPE '\\') "
        "ORDER BY roll_no COLLATE NOCASE ASC;";

    sqlite3_stmt* statement = nullptr;

    int rc = sqlite3_prepare_v2(
        db, sql, -1, &statement, nullptr
    );

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return {
            Status::DB_ERROR,
            {},
            "database query failed"
        };
    }

    // Always allow exact roll-number matching.
    rc = bindText(statement, 1, term);

    // Name matching is enabled only when authorized by the caller.
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(
            statement,
            2,
            allow_name_search ? 1 : 0
        );
    }

    // Escape user-supplied LIKE wildcards and search within names.
    const std::string pattern =
        "%" + escapeLike(term) + "%";

    if (rc == SQLITE_OK) {
        rc = bindText(statement, 3, pattern);
    }

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return {
            Status::DB_ERROR,
            {},
            "database query failed"
        };
    }

    std::vector<Student> students;

    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        Student student{};

        student.roll_no = columnText(statement, 0);
        student.name = columnText(statement, 1);
        student.department = columnText(statement, 2);
        student.semester =
            sqlite3_column_int(statement, 3);
        student.contact = columnText(statement, 4);
        student.cgpa = sqlite3_column_double(statement, 5);
        student.created_at = columnText(statement, 6);
        student.updated_at = columnText(statement, 7);

        students.push_back(std::move(student));
    }

    const int finalize_rc =
        sqlite3_finalize(statement);

    // Do not return partial results if the query failed.
    if (rc != SQLITE_DONE || finalize_rc != SQLITE_OK) {
        return {
            Status::DB_ERROR,
            {},
            "database query failed"
        };
    }

    return {
        Status::OK,
        std::move(students),
        ""
    };
}

Status StudentDAO::insert(const Student& student) {
    sqlite3* db = database_.handle();

    if (db == nullptr) {
        return Status::DB_ERROR;
    }

    // Acquire the write transaction before inserting.
    if (!execute(db, "BEGIN IMMEDIATE;")) {
        return Status::DB_ERROR;
    }

    const char* sql =
        "INSERT INTO students ("
        "roll_no, name, department, semester, contact, "
        "cgpa, created_at, updated_at"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* statement = nullptr;

    int rc = sqlite3_prepare_v2(
        db, sql, -1, &statement, nullptr
    );

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        rollback(db);
        return Status::DB_ERROR;
    }

    rc = bindText(statement, 1, student.roll_no);

    if (rc == SQLITE_OK) {
        rc = bindText(statement, 2, student.name);
    }

    if (rc == SQLITE_OK) {
        rc = bindText(statement, 3, student.department);
    }

    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(
            statement, 4, student.semester
        );
    }

    if (rc == SQLITE_OK) {
        rc = bindText(statement, 5, student.contact);
    }

    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_double(
            statement, 6, student.cgpa
        );
    }

    if (rc == SQLITE_OK) {
        rc = bindText(statement, 7, student.created_at);
    }

    if (rc == SQLITE_OK) {
        rc = bindText(statement, 8, student.updated_at);
    }

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        rollback(db);
        return Status::DB_ERROR;
    }

    rc = sqlite3_step(statement);

    const int extended_rc =
        sqlite3_extended_errcode(db);

    const int finalize_rc =
        sqlite3_finalize(statement);

    if (rc != SQLITE_DONE || finalize_rc != SQLITE_OK) {
        rollback(db);

        if (extended_rc == SQLITE_CONSTRAINT_PRIMARYKEY ||
            extended_rc == SQLITE_CONSTRAINT_UNIQUE) {
            return Status::DUPLICATE_KEY;
        }

        return Status::DB_ERROR;
    }

    // Do not report success until COMMIT succeeds.
    if (!execute(db, "COMMIT;")) {
        rollback(db);
        return Status::DB_ERROR;
    }

    return Status::OK;
}

Result<std::vector<Student>> StudentDAO::findByDeptSem(
    const std::string& department,
    int semester
) {
    sqlite3* db = database_.handle();

    if (db == nullptr) {
        return {
            Status::DB_ERROR,
            {},
            "database is not open"
        };
    }

    const char* sql =
        "SELECT roll_no, name, department, semester, "
        "contact, cgpa, created_at, updated_at "
        "FROM students "
        "WHERE department = ?1 COLLATE NOCASE "
        "AND semester = ?2 "
        "ORDER BY roll_no COLLATE NOCASE ASC;";

    sqlite3_stmt* statement = nullptr;

    int rc = sqlite3_prepare_v2(
        db, sql, -1, &statement, nullptr
    );

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return {
            Status::DB_ERROR,
            {},
            "database query failed"
        };
    }

    rc = bindText(statement, 1, department);

    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(statement, 2, semester);
    }

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return {
            Status::DB_ERROR,
            {},
            "database query failed"
        };
    }

    std::vector<Student> students;

    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        Student student{};

        student.roll_no = columnText(statement, 0);
        student.name = columnText(statement, 1);
        student.department = columnText(statement, 2);
        student.semester = sqlite3_column_int(statement, 3);
        student.contact = columnText(statement, 4);
        student.cgpa = sqlite3_column_double(statement, 5);
        student.created_at = columnText(statement, 6);
        student.updated_at = columnText(statement, 7);

        students.push_back(std::move(student));
    }

    const int finalize_rc = sqlite3_finalize(statement);

    if (rc != SQLITE_DONE || finalize_rc != SQLITE_OK) {
        return {
            Status::DB_ERROR,
            {},
            "database query failed"
        };
    }

    return {
        Status::OK,
        std::move(students),
        ""
    };
}

}  // namespace srms
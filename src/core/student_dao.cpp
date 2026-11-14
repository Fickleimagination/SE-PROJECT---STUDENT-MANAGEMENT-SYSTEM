#include "student_dao.h"

#include <sqlite3.h>

#include <limits>
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

Student readStudent(sqlite3_stmt* statement) {
    Student student{};

    student.roll_no = columnText(statement, 0);
    student.name = columnText(statement, 1);
    student.department = columnText(statement, 2);
    student.semester = sqlite3_column_int(statement, 3);
    student.contact = columnText(statement, 4);
    student.cgpa = sqlite3_column_double(statement, 5);
    student.created_at = columnText(statement, 6);
    student.updated_at = columnText(statement, 7);

    return student;
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

Result<std::vector<Student>> readStudentRows(
    sqlite3_stmt* statement
) {
    std::vector<Student> students;
    int rc = SQLITE_OK;

    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        students.push_back(readStudent(statement));
    }

    const int finalizeRc = sqlite3_finalize(statement);

    if (rc != SQLITE_DONE || finalizeRc != SQLITE_OK) {
        return {
            Status::DB_ERROR,
            {},
            "database query failed"
        };
    }

    return {Status::OK, std::move(students), ""};
}

int bindSearchParameters(
    sqlite3_stmt* statement,
    const std::string& term,
    bool allowNameSearch
) {
    int rc = bindText(statement, 1, term);

    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(
            statement, 2, allowNameSearch ? 1 : 0
        );
    }

    const std::string pattern = "%" + escapeLike(term) + "%";

    if (rc == SQLITE_OK) {
        rc = bindText(statement, 3, pattern);
    }

    return rc;
}

const char* sortColumn(Field field) {
    switch (field) {
        case Field::Department:
            return "department COLLATE NOCASE";
        case Field::Semester:
            return "semester";
        case Field::Cgpa:
            return "cgpa";
        default:
            return nullptr;
    }
}

const char* sortDirection(Order order) {
    switch (order) {
        case Order::Ascending:
            return "ASC";
        case Order::Descending:
            return "DESC";
        default:
            return nullptr;
    }
}

std::string buildFilterSql(const FilterCriteria& criteria) {
    std::string sql =
        "SELECT roll_no, name, department, semester, "
        "contact, cgpa, created_at, updated_at "
        "FROM students";

    if (criteria.department) {
        sql += " WHERE department = ?1 COLLATE NOCASE";
    }

    if (criteria.semester) {
        sql += criteria.department
            ? " AND semester = ?2"
            : " WHERE semester = ?1";
    }

    sql += " ORDER BY roll_no COLLATE NOCASE ASC;";

    return sql;
}

int bindFilterParameters(
    sqlite3_stmt* statement,
    const FilterCriteria& criteria
) {
    int parameterIndex = 1;
    int rc = SQLITE_OK;

    if (criteria.department) {
        rc = bindText(
            statement,
            parameterIndex++,
            *criteria.department
        );
    }

    if (rc == SQLITE_OK && criteria.semester) {
        rc = sqlite3_bind_int(
            statement,
            parameterIndex,
            *criteria.semester
        );
    }

    return rc;
}

}  // namespace

Result<Student> StudentDAO::find(
    const std::string& roll_no
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
        "LIMIT 1;";

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

    rc = bindText(statement, 1, roll_no);

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return {
            Status::DB_ERROR,
            {},
            "database query failed"
        };
    }

    rc = sqlite3_step(statement);

    if (rc == SQLITE_ROW) {
        Student student = readStudent(statement);
        const int finalize_rc = sqlite3_finalize(statement);

        if (finalize_rc != SQLITE_OK) {
            return {
                Status::DB_ERROR,
                {},
                "database query failed"
            };
        }

        return {
            Status::OK,
            std::move(student),
            ""
        };
    }

    const int finalize_rc = sqlite3_finalize(statement);

    if (rc == SQLITE_DONE && finalize_rc == SQLITE_OK) {
        return {
            Status::NOT_FOUND,
            {},
            "student not found"
        };
    }

    return {
        Status::DB_ERROR,
        {},
        "database query failed"
    };
}

Status StudentDAO::update(const Student& student) {
    sqlite3* db = database_.handle();

    if (db == nullptr) {
        return Status::DB_ERROR;
    }

    if (!execute(db, "BEGIN IMMEDIATE;")) {
        return Status::DB_ERROR;
    }

    const char* sql =
        "UPDATE students SET "
        "name = ?1, "
        "department = ?2, "
        "semester = ?3, "
        "contact = ?4, "
        "cgpa = ?5, "
        "created_at = ?6, "
        "updated_at = ?7 "
        "WHERE roll_no = ?8 COLLATE NOCASE;";

    sqlite3_stmt* statement = nullptr;

    int rc = sqlite3_prepare_v2(
        db, sql, -1, &statement, nullptr
    );

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        rollback(db);
        return Status::DB_ERROR;
    }

    rc = bindText(statement, 1, student.name);

    if (rc == SQLITE_OK) {
        rc = bindText(statement, 2, student.department);
    }

    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(
            statement, 3, student.semester
        );
    }

    if (rc == SQLITE_OK) {
        rc = bindText(statement, 4, student.contact);
    }

    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_double(
            statement, 5, student.cgpa
        );
    }

    if (rc == SQLITE_OK) {
        rc = bindText(statement, 6, student.created_at);
    }

    if (rc == SQLITE_OK) {
        rc = bindText(statement, 7, student.updated_at);
    }

    if (rc == SQLITE_OK) {
        rc = bindText(statement, 8, student.roll_no);
    }

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        rollback(db);
        return Status::DB_ERROR;
    }

    rc = sqlite3_step(statement);

    const int changed_rows = sqlite3_changes(db);
    const int finalize_rc = sqlite3_finalize(statement);

    if (rc != SQLITE_DONE || finalize_rc != SQLITE_OK) {
        rollback(db);
        return Status::DB_ERROR;
    }

    if (!execute(db, "COMMIT;")) {
        rollback(db);
        return Status::DB_ERROR;
    }

    return changed_rows > 0
        ? Status::OK
        : Status::NOT_FOUND;
}

Status StudentDAO::remove(const std::string& roll_no) {
    sqlite3* db = database_.handle();

    if (db == nullptr) {
        return Status::DB_ERROR;
    }

    if (!execute(db, "BEGIN IMMEDIATE;")) {
        return Status::DB_ERROR;
    }

    const char* sql =
        "DELETE FROM students "
        "WHERE roll_no = ?1 COLLATE NOCASE;";

    sqlite3_stmt* statement = nullptr;

    int rc = sqlite3_prepare_v2(
        db, sql, -1, &statement, nullptr
    );

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        rollback(db);
        return Status::DB_ERROR;
    }

    rc = bindText(statement, 1, roll_no);

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        rollback(db);
        return Status::DB_ERROR;
    }

    rc = sqlite3_step(statement);

    const int changed_rows = sqlite3_changes(db);
    const int finalize_rc = sqlite3_finalize(statement);

    if (rc != SQLITE_DONE || finalize_rc != SQLITE_OK) {
        rollback(db);
        return Status::DB_ERROR;
    }

    if (!execute(db, "COMMIT;")) {
        rollback(db);
        return Status::DB_ERROR;
    }

    return changed_rows > 0
        ? Status::OK
        : Status::NOT_FOUND;
}

Result<int> StudentDAO::count() {
    sqlite3* db = database_.handle();

    if (db == nullptr) {
        return {
            Status::DB_ERROR,
            0,
            "database is not open"
        };
    }

    const char* sql =
        "SELECT COUNT(*) FROM students;";

    sqlite3_stmt* statement = nullptr;

    int rc = sqlite3_prepare_v2(
        db, sql, -1, &statement, nullptr
    );

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return {
            Status::DB_ERROR,
            0,
            "database query failed"
        };
    }

    rc = sqlite3_step(statement);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(statement);
        return {
            Status::DB_ERROR,
            0,
            "database query failed"
        };
    }

    const sqlite3_int64 total =
        sqlite3_column_int64(statement, 0);

    const int finalize_rc = sqlite3_finalize(statement);

    if (finalize_rc != SQLITE_OK ||
        total > std::numeric_limits<int>::max()) {
        return {
            Status::DB_ERROR,
            0,
            "database count failed"
        };
    }

    return {
        Status::OK,
        static_cast<int>(total),
        ""
    };
}

Result<std::vector<Student>> StudentDAO::fetch(
    int offset,
    int limit
) {
    if (offset < 0 || limit < 0) {
        return {
            Status::VALIDATION_ERROR,
            {},
            "offset and limit must be non-negative"
        };
    }

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
        "ORDER BY roll_no COLLATE NOCASE ASC "
        "LIMIT ?1 OFFSET ?2;";

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

    rc = sqlite3_bind_int(statement, 1, limit);

    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(statement, 2, offset);
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
        students.push_back(readStudent(statement));
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
        return {Status::DB_ERROR, {}, "database is not open"};
    }

    const char* sql =
        "SELECT roll_no, name, department, semester, "
        "contact, cgpa, created_at, updated_at "
        "FROM students "
        "WHERE roll_no = ?1 COLLATE NOCASE "
        "OR (?2 = 1 AND name LIKE ?3 ESCAPE '\\') "
        "ORDER BY roll_no COLLATE NOCASE ASC;";

    sqlite3_stmt* statement = nullptr;

    const int prepareRc =
        sqlite3_prepare_v2(db, sql, -1, &statement, nullptr);

    if (prepareRc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return {Status::DB_ERROR, {}, "database query failed"};
    }

    const int bindRc =
        bindSearchParameters(statement, term, allow_name_search);

    if (bindRc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return {Status::DB_ERROR, {}, "database query failed"};
    }

    return readStudentRows(statement);
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
        students.push_back(readStudent(statement));
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

// US-10: Sort the complete student list.

Result<std::vector<Student>> StudentDAO::sortRecords(
    Field field,
    Order order
) {
    sqlite3* db = database_.handle();

    if (db == nullptr) {
        return {Status::DB_ERROR, {}, "database is not open"};
    }

    const char* column = sortColumn(field);
    const char* direction = sortDirection(order);

    if (column == nullptr) {
        return {Status::VALIDATION_ERROR, {}, "invalid sort field"};
    }

    if (direction == nullptr) {
        return {Status::VALIDATION_ERROR, {}, "invalid sort order"};
    }

    const std::string sql =
        "SELECT roll_no, name, department, semester, "
        "contact, cgpa, created_at, updated_at "
        "FROM students ORDER BY " +
        std::string(column) + " " + direction +
        ", roll_no COLLATE NOCASE ASC;";

    sqlite3_stmt* statement = nullptr;

    const int rc =
        sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr);

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return {Status::DB_ERROR, {}, "database query failed"};
    }

    return readStudentRows(statement);
}

// US-11: Filter by department, semester, or both.

Result<std::vector<Student>> StudentDAO::filterRecords(
    const FilterCriteria& criteria
) {
    sqlite3* db = database_.handle();

    if (db == nullptr) {
        return {Status::DB_ERROR, {}, "database is not open"};
    }

    if (!criteria.department && !criteria.semester) {
        return {
            Status::VALIDATION_ERROR,
            {},
            "at least one filter criterion is required"
        };
    }

    const std::string sql = buildFilterSql(criteria);
    sqlite3_stmt* statement = nullptr;

    const int prepareRc =
        sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr);

    if (prepareRc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return {Status::DB_ERROR, {}, "database query failed"};
    }

    const int bindRc = bindFilterParameters(statement, criteria);

    if (bindRc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return {Status::DB_ERROR, {}, "database query failed"};
    }

    return readStudentRows(statement);
}


}  // namespace srms
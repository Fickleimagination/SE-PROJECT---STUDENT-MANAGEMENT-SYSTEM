
-- SRMS database schema
-- Project: Student Record Management System
-- Requirements: REQ-DATA-01, REQ-DATA-06, REQ-DATA-08, SEC-02

PRAGMA foreign_keys = ON;

-- Student academic records
CREATE TABLE IF NOT EXISTS students (
    roll_no TEXT NOT NULL COLLATE NOCASE PRIMARY KEY
        CHECK (
            length(roll_no) BETWEEN 5 AND 20
            AND roll_no NOT GLOB '*[^A-Z0-9]*'
        ),

    name TEXT NOT NULL
        CHECK (length(trim(name)) BETWEEN 1 AND 80),

    department TEXT NOT NULL
        CHECK (
            department IN (
                'CSE', 'ECE', 'EEE', 'ME',
                'CV', 'BT', 'AIML'
            )
        ),

    semester INTEGER NOT NULL
        CHECK (
            typeof(semester) = 'integer'
            AND semester BETWEEN 1 AND 8
        ),

    contact TEXT NOT NULL DEFAULT ''
        CHECK (length(contact) <= 50),

    cgpa REAL NOT NULL
        CHECK (cgpa >= 0.0 AND cgpa <= 10.0),

    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

-- Administrator authentication data
CREATE TABLE IF NOT EXISTS admins (
    username TEXT NOT NULL COLLATE NOCASE PRIMARY KEY,
    password_hash TEXT NOT NULL,
    salt TEXT NOT NULL,
    role TEXT NOT NULL,

    failed_attempts INTEGER NOT NULL DEFAULT 0
        CHECK (failed_attempts >= 0),

    locked_until INTEGER NOT NULL DEFAULT 0,

    must_change_password INTEGER NOT NULL DEFAULT 1
        CHECK (must_change_password IN (0, 1))
);

-- Security audit events
CREATE TABLE IF NOT EXISTS audit_log (
    event_id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT NOT NULL,
    username TEXT NOT NULL,
    event_type TEXT NOT NULL,
    target_roll_no TEXT,
    outcome TEXT NOT NULL
);

-- Supports department and semester queries.
CREATE INDEX IF NOT EXISTS idx_students_department_semester
    ON students(department, semester, roll_no);

# Student Record CRUD module: conformance and deviations (US-03, US-04, US-05, US-06, US-14)

Owner: Bhavyaa Garg. Companion to `DEVIATIONS.md` (Reports module).

## 1. Built as specified

| Requirement | Result |
|---|---|
| UC-03 Add student (REQ-CRUD-01..02) | All six fields validated, 3 attempts per field, duplicate roll_no rejected, success and error messages as in the SRS |
| UC-04 Update student (REQ-CRUD-03..04) | Partial update through `StudentPatch`; unknown roll_no gives "ERROR: roll_no: record not found." |
| UC-05 Delete student (REQ-CRUD-05) | Only the exact text `YES` confirms; anything else prints "Deletion cancelled." |
| UC-06 List students (REQ-CRUD-06) | 20 records per page, ordered by roll_no |
| Data and security (REQ-DATA, SEC-07) | Parameterized SQL only; records persist after close and reopen; operations need a valid session |
| Tests (TC-CRUD-01..05, TC-DATA-02, TC-SEC-07) | Mapped to real tests in `docs/test-reports/Record_Module_Test_Traceability.md`; all tests pass locally and in CI |

## 2. Design refinements (small, deliberate)

| # | Document says | What we did | Why |
|---|---|---|---|
| 1 | SAD 4.3.2: RecordService uses StudentDAO | RecordService uses a `StudentRepository` interface that StudentDAO implements | Tests run against an in-memory fake without SQLite |
| 2 | SAD lists four service methods | Added `listStudents` and `isDeleteConfirmed` | UC-06 needs paging; "exactly YES" needs a testable rule |
| 3 | SAD: ConsoleUI owns menus | Records submenu lives in `RecordController`, attached via `setRecordController()` | Keeps console I/O out of the service and leaves `ConsoleUI`'s tested constructor unchanged |
| 4 | Sessions checked by services | Also reject a future `last_activity` | Hardening from review (PR #27) |
| 5 | SAD: one shared audit logger class | Each service takes an audit callback; `main()` passes one that writes to `audit_log` (record add, update and delete store the username and roll_no) | The shared logger class was not built; a callback keeps services free of SQLite |
| 6 | SRS leaves details open | Apostrophe accepts `'` and the typographic form; paging keys are N / P / Q | Appendix B writes the typographic form; SRS defines no keys |

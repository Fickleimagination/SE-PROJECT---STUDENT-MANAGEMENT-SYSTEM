# SRMS — Student Record Management System
  
A console-based C++17 application for managing student academic records, built for the
Software Engineering (UE24CS341A) Jackfruit Mini-Project at PES University, Bangalore.

**Team 17** | Project ID: SRMS-2026-17 | Group: G-17 | Methodology: Agile (GitHub Projects)

[![CI](https://github.com/Fickleimagination/SE-PROJECT---STUDENT-MANAGEMENT-SYSTEM/actions/workflows/ci.yml/badge.svg)](https://github.com/Fickleimagination/SE-PROJECT---STUDENT-MANAGEMENT-SYSTEM/actions/workflows/ci.yml)

## Team

| Member | SRN | Owned functionality |
|---|---|---|
| B Varun | PES1UG24CS109 | Administrator authentication, session management, console menu |
| Bhavyaa Garg | PES1UG24CS116 | Student record CRUD, field validation |
| Chaithanya N | PES1UG24CS124 | Search, sort and filter; SQLite schema and query design; code quality gates |
| Arabhi Bhat (Team Lead) | PES1UG24CS927 | Reports and statistics (class list, average CGPA, topper list, TXT/CSV export), CI setup |

## About

SRMS replaces paper registers with a single offline, single-machine console application
backed by SQLite. An authenticated Administrator can add, update, delete, search, sort,
filter and report on student records. A Student/Viewer can look up a single record
read-only, without contact details.

## Features

| Feature | Requirements | Owner |
|---|---|---|
| Administrator login, lockout, forced password change, idle-session expiry | REQ-AUTH-* | Varun |
| Add / update / delete / list student records with field validation | REQ-CRUD-*, REQ-DATA-* | Bhavyaa |
| Search by roll number (exact, case-insensitive) or name (admin, partial, min. 2 chars) | REQ-SRCH-01..04 | Chaithanya |
| Sort by department, semester or CGPA (asc/desc, ties by roll number) | REQ-SRCH-05..06 | Chaithanya |
| Filter by department, semester, or both | REQ-SRCH-07 | Chaithanya |
| Class list, average CGPA, topper list, export to TXT/CSV | REQ-RPT-* | Arabhi |
| Persistent storage in SQLite with parameterised queries | REQ-DATA-*, SEC-02 | All |

## Architecture

Layered architecture (SAD section 3.5):

```
Console UI -> Controller -> Service -> DAO -> SQLite
```

Layers only call the layer directly below them. Business logic in `src/core` does not do
console I/O (NFR-MNT-01); the console entry point lives in `src/main.cpp`.

| Layer | Components |
|---|---|
| Presentation | `ConsoleUI`, `main.cpp` menus |
| Controller | `RecordController` |
| Service | `AuthService`, `RecordService`, `SearchService`, `ReportService`, `Validator` |
| Data access | `AdminDAO`, `StudentDAO`, `Database` |
| Data | SQLite (`srms.db`, schema in `sql/schema.sql`) |

## Repository layout

```
src/main.cpp          application entry point
src/core/             services, DAOs, validator, console UI
sql/schema.sql        database schema
tests/                GoogleTest unit tests
docs/SRS, SAD, TestPlan   specification documents (source)
docs/test-reports/    unit test, code coverage and branch coverage reports
DEVIATIONS.md         documented deviations from SRS / SAD / Test Plan
.github/workflows/    CI pipeline
```

## Build and run

### Dependencies

- CMake 3.20+, a C++17 compiler (g++ / MinGW-w64)
- SQLite3 development library
- OpenSSL development library (password hashing)
- GoogleTest

**Ubuntu**
```bash
sudo apt-get install cmake ninja-build g++ libsqlite3-dev libssl-dev libgtest-dev
```

**Windows (MSYS2 UCRT64 shell)**
```bash
pacman -S mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,sqlite3,openssl,gtest}
```

### Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Run

Run from the repository root. The application opens `srms.db` and reads
`sql/schema.sql` using paths relative to the working directory.

```bash
./build/srms_app          # Windows: ./build/srms_app.exe
```

Exported reports are written to `./reports/`.

> **Administrator account:** the schema does not seed an account. Create the first
> administrator as described by the Authentication owner before running the demo.
> <!-- TODO (Varun): document the provisioning step or seeded credentials here. -->

## Testing

```bash
ctest --test-dir build --output-on-failure
```

### Coverage (same commands as CI)

```bash
cmake -S . -B build-ci -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  "-DCMAKE_CXX_FLAGS=--coverage -O0 -g" "-DCMAKE_EXE_LINKER_FLAGS=--coverage"
cmake --build build-ci
ctest --test-dir build-ci --output-on-failure
gcovr --root . --filter 'src/core/' --print-summary --fail-under-line 70 build-ci
gcovr --root . --filter 'src/core/' --branches --print-summary build-ci   # branch coverage
```

### Quality gates (NFR-MNT-02, NFR-MNT-03, NFR-TST-01)

| Gate | Target | Command |
|---|---|---|
| Compiler warnings | 0 with `-Wall -Wextra -Wpedantic` | see `CMakeLists.txt` |
| Static analysis | 0 Cppcheck errors | `cppcheck --enable=warning,performance,portability --std=c++17 -I src/core src/core` |
| Complexity / length | CCN <= 10, length <= 60 lines | `lizard src/core -C 10 -L 60 -w` |
| Line coverage | >= 70% of `src/core` | `gcovr ... --fail-under-line 70` |

## CI

GitHub Actions (`.github/workflows/ci.yml`) runs on every push and pull request on
Ubuntu 24.04: build, all tests, line-coverage gate (70%) and Cppcheck, with a 10-minute
timeout (NFR-TST-02).

## Documentation

| Document | Location |
|---|---|
| Combined SRS + SAD + Test Plan (PDF) | [`docs/Team17_SRMS_SRS_SAD_STP.pdf`](./docs/Team17_SRMS_SRS_SAD_STP.pdf) |
| SRS v2 | [`docs/SRS/`](./docs/SRS) |
| SAD | [`docs/SAD/`](./docs/SAD) |
| Test Plan | [`docs/TestPlan/`](./docs/TestPlan) |
| Test and coverage reports | [`docs/test-reports/`](./docs/test-reports) |
| Deviations from the submitted documents | [`DEVIATIONS.md`](./DEVIATIONS.md) |

## Agile process

Work is tracked on the GitHub Projects board linked to this repository: backlog items
derived from the SRS FRs and NFRs, story points, per-member assignment, and two planned
sprints. Each user story is delivered through a pull request that closes its issue and is
reviewed by the repository owner.

## Status

- Requirements, architecture/design and test plan: complete (Deliverables Part 1).
- Sprint 1 and Sprint 2 implementation: authentication, record CRUD, search/sort/filter
  services and DAO, reports and export, CI.
- Testing and reports (Deliverables Part 3): in progress. See `docs/test-reports/`.

## License

Academic project for UE24CS341A, PES University, 2026.

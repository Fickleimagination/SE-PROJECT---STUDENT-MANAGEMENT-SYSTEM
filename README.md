# SRMS — Student Record Management System

A console-based C/C++ application for managing student academic records, built for
the Software Engineering (UE24CS341A) Jackfruit Mini-Project at PES University.

**Team 17** | Project ID: SRMS-2026-17 | Complexity: Easy | Technology: C / C++

## Team

| Member | SRN | Module Owned |
|---|---|---|
| B Varun | PES1UG24CS109 | Administrator Authentication & Session Management |
| Bhavyaa Garg | PES1UG24CS116 | Student Record CRUD |
| Chaithanya N | PES1UG24CS124 | Search, Sort & Filter |
| Arabhi Bhat (Team Lead) | PES1UG24CS927 | Reports & Statistics |

## About

SRMS digitizes student record keeping for a college department's academic office,
replacing manual registers with a single console application backed by a SQLite
database. An authenticated Administrator can create, update, delete, search,
sort/filter and report on student records; a Student/Viewer can look up an
individual record read-only.

## Project Documentation

All formal documentation for this project lives in [`docs/`](./docs):

- 📄 **[Combined SRS + SAD + STP (PDF)](./docs/Team17_SRMS_SRS_SAD_STP.pdf)** — the full submission for Deliverables Part-1
- [`docs/SRS/`](./docs/SRS) — Software Requirements Specification (source)
- [`docs/SAD/`](./docs/SAD) — Software Architecture & Design Specification (source)
- [`docs/TestPlan/`](./docs/TestPlan) — Software Test Plan (source)

## Tech Stack

- **Language:** C++17
- **Build:** GNU Make / CMake
- **Database:** SQLite 3.31+ (via the sqlite3 C API)
- **Crypto:** OpenSSL libcrypto (password hashing)
- **Testing:** GoogleTest, cppcheck, Valgrind
- **CI/CD:** GitHub Actions

## Getting Started

```bash
# clone
git clone https://github.com/Fickleimagination/SE-PROJECT---STUDENT-MANAGEMENT-SYSTEM.git
cd srms

# build (once source is added)
make
./srms
```

## Status

🚧 In development — Requirements and Design phases complete; Implementation in progress.

## License

Academic project for UE24CS341A, PES University, 2026.

# Reports module (C++17) — US-09, US-12, US-13

Owner: Arabhi Bhat. Implements SRS 5.4 (REQ-RPT-01..05) per SAD 4.2.6 / 4.3.2.

## Files
- `src/core/report_service.{h,cpp}`: Class List, Average CGPA, Topper List, export to TXT/CSV, session checks, audit hook. No console I/O (NFR-MNT-01).
- `src/core/types.h`: TEMPORARY stub of the shared types. Replace with Bhavyaa Garg's `types.h` when it is merged; names and field order must stay the same.
- `tests/report_service_test.cpp`: GoogleTest suite (13 cases).

## Build and test
Requires g++ 11+ (C++17) and GoogleTest 1.12+.

    g++ -std=c++17 -Wall -Wextra -Wpedantic -Isrc/core \
        tests/report_service_test.cpp src/core/report_service.cpp \
        -lgtest -lgtest_main -pthread -o report_tests
    ./report_tests

Note: the sqlite3 student source is not included here. `StudentSource` is an interface, so the tests use a fake. The real implementation is `StudentDAO` (owner: Chaithanya N).

## Decisions and open points
- Empty selection: SRS REQ-RPT-05 (message, no file) is followed. SAD 4.3.2 says "empty is OK with zero rows". Confirm with the team.
- TXT is tab-separated. The SRS does not specify the TXT layout.
- Export path is `./reports/<type>_<YYYYMMDD_HHMMSS>.<ext>`, returned as an absolute path (REQ-RPT-04). Two exports within the same second overwrite each other.
- CGPA ties are compared in hundredths (`llround(cgpa*100)`), so 9.80 and 9.8 are equal.

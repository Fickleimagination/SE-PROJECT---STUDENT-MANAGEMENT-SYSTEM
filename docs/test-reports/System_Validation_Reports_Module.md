# System Validation Report: Reports & Statistics

**Owner:** Arabhi Bhat
**Test plan reference:** Team17_SRMS_TestPlan_v1.1, Section 13.6.5 (TC-RPT-01 to TC-RPT-03)
**Date:** 2026-11-16
**Build:** commit fb22498 on main (includes fix 8878973)
**Environment:** macOS, AppleClang 21.0.0, SQLite 3.51.0, OpenSSL 4.1.0, console UI
**Test data:** PES1UG24CS001 (7.20), PES1UG24CS002 (8.50), PES1UG24CS003 (9.10), all CSE, semester 5

## Results

| ID | Requirement | Steps | Expected | Actual | Result |
|---|---|---|---|---|---|
| TC-RPT-01 | REQ-RPT-05 | Reports > Class list; CSE; semester 1 (no students) | "No records available for this report."; no file created | Message printed; no file created | PASS after fix 8878973 (FAIL before fix: only headers printed) |
| TC-RPT-02 | REQ-RPT-02 | Reports > Average CGPA; CSE; semester 5 | 8.27 (mean of 7.20, 8.50, 9.10 = 8.2667) | Students: 3; Average CGPA: 8.27 | PASS |
| TC-RPT-03 | REQ-RPT-03, REQ-RPT-04, SEC-04 | Reports > Topper list; CSE; semester 5; export CSV; check audit_log | Topper listed; file matches screen; one REPORT_EXPORTED|OK row | PES1UG24CS003 (9.10) listed; saved to ./reports/topper_list_20261116_202528.csv; audit row REPORT_EXPORTED|OK | PASS after fix 8878973 (FAIL before fix: no audit row) |

## Evidence

- TC-RPT-01: terminal output showing "No records available for this report."
- TC-RPT-02: terminal output showing Students: 3 and Average CGPA: 8.27
- TC-RPT-03: terminal output showing "Saved to ..." and the query result `REPORT_EXPORTED|OK`

## Open items (not yet recorded)

- **TC-RPT-02 timing:** the test requires the report within 2 seconds (NFR-PERF-02). The report appeared instantly on screen, but this has not been measured with a timer. Record a measured time.
- **TC-RPT-03 file comparison:** step 4 (open the exported CSV and compare it to the screen) has not been done yet.
- **TC-RPT-03 ties:** the test expects every tied topper to be listed. The test data has no ties, so this case is not covered yet.

## Sign-off

Tester: Arabhi Bhat
Approval: ____________________

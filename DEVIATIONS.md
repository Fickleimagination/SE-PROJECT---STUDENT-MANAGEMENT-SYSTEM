# Deviations from SRS, SAD, and Test Plan (Reports module, US-09 / US-12 / US-13)

| # | Document says | What we built | Reason |
|---|---|---|---|
| 1 | SAD 4.3.2: ReportService depends on StudentDAO | ReportService depends on a `StudentSource` interface, implemented by StudentDAO | Lets the report tests run without SQLite. |
| 2 | SAD 4.3.2: an empty dataset is OK with zero rows | SRS REQ-RPT-05 followed: show "No records available for this report." and create no file | The SRS is the requirement; the SAD wording conflicts. Team to confirm. |
| 3 | SRS does not define the TXT layout | TXT exports are tab-separated, with a header row | Chosen so TXT and CSV carry the same columns. |
| 4 | SRS REQ-RPT-04: `./reports/<type>_<YYYYMMDD_HHMMSS>` | Path follows this format and the absolute path is returned. Two exports in the same second overwrite each other. | Timestamp granularity is one second. |
| 5 | SAD 4.4.1: session errors are handled by services | Future `last_activity` timestamps are rejected, in addition to idle sessions over 600 s | Found in review of PR #26. Not in the SAD. |
| 6 | SRS NFR-PORT-01: build on Ubuntu and Windows/MinGW | Reports code is tested on macOS with GoogleTest 1.18. Windows/MinGW not yet tested. | Not yet verified on the Windows target. |
| 7 | SRS 2.5: GitHub Actions runs build and tests on every push | The CI workflow lives in PR #28 and is not on `main` yet | Pending merge of PR #28. |
| 8 | SRS REQ-RPT-03: Topper List includes all tied students | Implemented as specified | No deviation. Listed for traceability. |

## Open items for the team

- Confirm deviation 2 (empty-report rule) and update whichever document is wrong.
- Run the reports tests on Windows/MinGW before the final demo.
- Re-run the CI check on PR #29 once PR #28 merges.

# Student Record module: test traceability (Sprint 2 update)

Owner: Bhavyaa Garg. Maps the Test Plan cases for the record module (TC-CRUD-01..05, TC-DATA-02, TC-SEC-07)
to the automated GoogleTest cases that run in CI. Run with `ctest --test-dir build` (134 tests, all passing at the
time of writing). Names are `Suite.TestName`.

## Update to the Test Plan v1.1, Section 13.6

The "Automated equivalent" lines for TC-03 and TC-04 name tests that do not exist under those names. The real tests are:

| Test Plan v1.1 names | Actual automated tests |
|---|---|
| `RecordServiceTest.AddValidStudentPersistsAcrossReopen` | `RecordServiceAddTest.AddsValidRecordWithAllSixFields` and `StudentDAOTest.StudentPersistsAfterCloseAndReopen` |
| `ConsoleUITest.ConfirmDeletionAcceptsOnlyExactYES` | `RecordServiceConfirm.OnlyExactYesConfirms` and `RecordControllerTest.AnythingOtherThanExactYesCancelsDeletion` |
| `RecordServiceTest.DeleteMissingRollReturnsNotFound` | `RecordServiceDeleteTest.UnknownRollNoIsNotFoundAndChangesNothing` |

## Traceability

| Test Plan case | Requirements | Automated tests |
|---|---|---|
| TC-CRUD-01 Add a valid record | REQ-CRUD-01, 08; REQ-DATA-01 | `RecordServiceAddTest`: AddsValidRecordWithAllSixFields, ContactIsOptional, StoresRollNoUpperCaseAndNameTrimmed, SetsBothTimestampsInRequiredFormat. `RecordControllerTest`: AddsRecordAndConfirmsWithRollNumber, PromptsInTheOrderTheSrsGives, ContactCanBeSkippedWithEnter. `StudentDAOTest`: InsertAndFindStudent, StudentPersistsAfterCloseAndReopen |
| TC-CRUD-02 Duplicate roll_no | REQ-CRUD-02; REQ-DATA-06 | `RecordServiceAddTest`: RejectsDuplicateRollNoAndKeepsCountUnchanged, DuplicateIsDetectedIgnoringRollNoCase, PassesThroughDuplicateKeyFromRepository. `RecordControllerTest`: DuplicateRollNumberShowsSrsError. `StudentDAOTest`: DuplicateRollNumberIsRejected |
| TC-CRUD-03 Update fields, Enter keeps value | REQ-CRUD-03, 04, 08 | `RecordServiceUpdateTest`: ChangesEachEditableField, KeepsFieldsNotInThePatch, EmptyPatchChangesOnlyUpdatedAt, ClearingContactIsAllowed. `RecordControllerTest`: UpdateChangesTypedFieldsAndKeepsTheRestOnEnter, UpdateShowsCurrentRecordAndValuesInBrackets. `StudentDAOTest`: UpdateChangesExistingStudent |
| TC-CRUD-04 Delete needs exact YES | REQ-CRUD-05, 07; NFR-SAFE-01 | `RecordServiceConfirm`: OnlyExactYesConfirms. `RecordServiceDeleteTest`: DeletesExistingRecord, UnknownRollNoIsNotFoundAndChangesNothing, AuditsRecordDeleted. `RecordControllerTest`: DeleteWithExactYesRemovesRecord, AnythingOtherThanExactYesCancelsDeletion, DeleteOfUnknownRollNumberShowsNotFound, DeleteShowsTheRecordBeforeAskingForConfirmation. `StudentDAOTest`: RemoveDeletesExistingStudent, RemoveMissingStudentReturnsNotFound |
| TC-CRUD-05 List 45 records, 20 per page | REQ-CRUD-06 | `RecordServiceListTest`: ThreePagesOfTwentyTwentyFive, OrderedByRollNoAscendingAcrossPages, PageOutOfRangeIsValidationError, ExactMultipleOfPageSizeHasNoEmptyLastPage. `RecordServiceListEmpty`: EmptyDatabaseIsOkWithEmptyPageOne. `RecordControllerTest`: FortyFiveRecordsPageThroughThreePages, PreviousOnFirstPageIsRefused, SinglePageListShowsColumnsAndRecords. `StudentDAOTest`: FetchReturnsStudentsInRollNumberOrder, FetchReturnsCorrectPage |
| TC-DATA-02 Field validation | REQ-DATA-02..05 | `ValidatorRollNo`, `ValidatorName`, `ValidatorSemester`, `ValidatorCgpa`, `ValidatorDepartment`, `ValidatorContact`, `ValidatorStudent` suites. `RecordServiceAddTest.RejectsEachInvalidFieldAndSavesNothing`. `RecordServiceUpdateTest.RejectsInvalidValuesAndKeepsStoredRecord`. `RecordControllerTest`: BadFieldIsAskedAgainWithFieldSpecificError, ThreeWrongAttemptsCancelTheOperation |
| TC-SEC-07 Admin-only changes | REQ-AUTH-06; SEC-07 | `ViewerSessionIsDenied` / `IdleSessionIsDenied` in `RecordServiceAddTest`; `ViewerAndIdleSessionsAreDenied` in `RecordServiceUpdateTest`, `RecordServiceDeleteTest` and `RecordServiceListTest`; `RecordControllerTest.ViewerSessionCannotAdd` |

## Not yet covered

- **Running the real program** (the step-by-step system tests in Section 13.6, the 1 s commit time in TC-PERF-03, the 80-column check in UI-04). The program has no `main()` yet, so these were not executed.
- **Audit rows in the database** (SEC-04). The record service calls its audit callback and a test checks it, but nothing writes to the `audit_log` table yet.
- **Windows/MinGW** (NFR-PORT-01). Tests run on macOS and Ubuntu only.

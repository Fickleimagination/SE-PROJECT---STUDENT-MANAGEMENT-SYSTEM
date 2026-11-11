// Console menu for student records (UC-03 Add, UC-04 Update, UC-05 Delete, UC-06 List).
// Owner: Bhavyaa Garg. SRS 5.2, SAD 4.2.2. All prompts and messages follow the SRS use cases;
// every rule is enforced again inside RecordService, so this class only talks to the user.
#pragma once
#include <iosfwd>
#include <string>

#include "record_service.h"
#include "types.h"
#include "validator.h"

namespace srms {

class RecordController {
public:
    RecordController(RecordService& service, std::istream& input, std::ostream& output,
                     Validator validator = Validator());

    // Shows the "Student Records" submenu until the administrator chooses Back (or input ends).
    // Refreshes session.last_activity after every line read (REQ-AUTH-08).
    void run(Session& session);

private:
    enum class Answer { Value, Keep, Cancelled };

    bool readLine(Session& session, const std::string& prompt, std::string& line);
    Answer askField(Session& session, const std::string& field, const std::string& prompt,
                    bool allowKeep, std::string& value);
    void showRecord(const Student& student);
    void report(Status status, const std::string& operation, const std::string& roll_no);

    void addRecord(Session& session);
    void updateRecord(Session& session);
    void deleteRecord(Session& session);
    void listRecords(Session& session);

    RecordService& service_;
    std::istream& input_;
    std::ostream& output_;
    Validator validator_;
};

}  // namespace srms

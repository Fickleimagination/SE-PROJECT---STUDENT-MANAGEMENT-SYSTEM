#pragma once

#include <iosfwd>
#include <string>

#include "auth_service.h"

namespace srms {

class RecordController;

class ConsoleUI {
public:
    ConsoleUI(
        AuthService& auth,
        std::istream& input,
        std::ostream& output);

    void run();

    // Connects the Student records menu (US-03..US-06). Without it, option 1 says not connected.
    void setRecordController(RecordController* records) { records_ = records; }

private:
    AuthService& auth_;
    std::istream& input_;
    std::ostream& output_;
    RecordController* records_ = nullptr;

    bool promptLine(
        const std::string& message,
        std::string& value);

        bool promptPassword(const std::string& message, std::string& value);

    bool login(Session& session);

    bool requirePasswordChange(Session& session);

    void adminMenu(Session& session);
};

} // namespace srms
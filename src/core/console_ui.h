#pragma once

#include <iosfwd>
#include <string>

#include "auth_service.h"

namespace srms {

class ConsoleUI {
public:
    ConsoleUI(
        AuthService& auth,
        std::istream& input,
        std::ostream& output);

    void run();

private:
    AuthService& auth_;
    std::istream& input_;
    std::ostream& output_;

    bool promptLine(
        const std::string& message,
        std::string& value);

    bool login(Session& session);

    bool requirePasswordChange(Session& session);

    void adminMenu(Session& session);
};

} // namespace srms
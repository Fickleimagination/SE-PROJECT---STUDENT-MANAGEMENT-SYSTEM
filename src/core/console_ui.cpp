#include "console_ui.h"

#include <ctime>
#include <iostream>
#include <string>

namespace srms {

ConsoleUI::ConsoleUI(
    AuthService& auth,
    std::istream& input,
    std::ostream& output)
    : auth_(auth), input_(input), output_(output) {}

bool ConsoleUI::promptLine(
    const std::string& message,
    std::string& value) {

    output_ << message;
    output_.flush();

    return static_cast<bool>(std::getline(input_, value));
}

bool ConsoleUI::login(Session& session) {
    std::string username;
    std::string password;

    if (!promptLine("Username: ", username)) {
        return false;
    }

    if (!promptLine("Password: ", password)) {
        return false;
    }

    auto result = auth_.login(username, password);

    if (result.status != Status::OK) {
        output_ << "Login failed: "
                << result.detail << '\n';
        return false;
    }

    session = result.value;
    output_ << "Login successful.\n";
    return true;
}

bool ConsoleUI::requirePasswordChange(Session& session) {
    auto required = auth_.mustChangePassword(session);

    if (required.status != Status::OK) {
        output_ << "Unable to verify password-change requirement.\n";
        return false;
    }

    if (!required.value) {
        return true;
    }

    output_ << "\nYou must change your password before continuing.\n";

    while (input_) {
        std::string password;
        std::string confirmation;

        if (!promptLine("New password: ", password) ||
            !promptLine("Confirm new password: ", confirmation)) {
            return false;
        }

        if (password != confirmation) {
            output_ << "Passwords do not match. Try again.\n";
            continue;
        }

        if (!AuthService::passwordMeetsPolicy(password)) {
            output_ << "Password must contain at least 8 characters, "
                       "including a letter and a digit.\n";
            continue;
        }

        Status result = auth_.changePassword(session, password);

        if (result == Status::OK) {
            output_ << "Password changed successfully.\n";
            return true;
        }

        output_ << "Password change failed. Please try again.\n";
    }

    return false;
}

void ConsoleUI::adminMenu(Session& session) {
    while (input_) {
        if (!auth_.isActiveAdmin(session)) {
            output_ << "\nSession expired. Please log in again.\n";
            return;
        }

        output_ << "\n===== SRMS Administrator Menu =====\n"
                << "1. Student records\n"
                << "2. Reports and statistics\n"
                << "3. Logout\n"
                << "Enter your choice: ";

        std::string choice;

        if (!std::getline(input_, choice)) {
            return;
        }

        // Refresh activity after receiving an input.
        session.last_activity = std::time(nullptr);

        if (choice == "1") {
            output_ << "Student-record operations are not connected yet.\n";
        } else if (choice == "2") {
            output_ << "Reporting operations are not connected yet.\n";
        } else if (choice == "3") {
            auth_.logout(session);
            output_ << "Logged out successfully.\n";
            return;
        } else {
            output_ << "Invalid choice. Enter 1, 2, or 3.\n";
        }
    }
}

void ConsoleUI::run() {
    while (input_) {
        Session session{"", false, 0};

        output_ << "\n===== Student Record Management System =====\n";

        if (!login(session)) {
            if (!input_) {
                return;
            }
            continue;
        }

        if (!requirePasswordChange(session)) {
            auth_.logout(session);

            if (!input_) {
                return;
            }
            continue;
        }

        adminMenu(session);
        auth_.logout(session);
    }
}

} // namespace srms
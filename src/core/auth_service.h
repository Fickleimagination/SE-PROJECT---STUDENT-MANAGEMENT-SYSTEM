#pragma once

#include <ctime>
#include <functional>
#include <optional>
#include <string>

#include "types.h"

namespace srms {

struct AdminAccount {
    std::string username;
    std::string password_hash;
    std::string salt;
    int failed_attempts = 0;
    std::time_t locked_until = 0;
    bool must_change_password = true;
};

class AdminRepository {
public:
    virtual ~AdminRepository() = default;

    virtual Result<std::optional<AdminAccount>>
    findByUsername(const std::string& username) = 0;

    virtual Status updateLoginState(
        const std::string& username,
        int failed_attempts,
        std::time_t locked_until) = 0;

    virtual Status updatePassword(
        const std::string& username,
        const std::string& password_hash,
        const std::string& salt,
        bool must_change_password) = 0;
};

using AuthAuditHook = std::function<void(
    const std::string& event,
    const std::string& username,
    const std::string& outcome)>;

using AuthClock = std::function<std::time_t()>;

class AuthService {
public:
    AuthService(
        AdminRepository& repository,
        AuthAuditHook audit = nullptr,
        AuthClock clock = nullptr);

    Result<Session> login(
        const std::string& username,
        const std::string& password);

        Result<bool> mustChangePassword(const Session& session) const;
    Status changePassword(
        Session& session,
        const std::string& new_password);

    void logout(Session& session);

    bool isActiveAdmin(const Session& session) const;

    static bool passwordMeetsPolicy(
        const std::string& password);

private:
    AdminRepository& repository_;
    AuthAuditHook audit_;
    AuthClock clock_;
};

} // namespace srms
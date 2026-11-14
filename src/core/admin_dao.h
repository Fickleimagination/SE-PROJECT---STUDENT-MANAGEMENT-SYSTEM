#pragma once

#include "auth_service.h"
#include "database.h"

namespace srms {

class AdminDAO final : public AdminRepository {
public:
    explicit AdminDAO(Database& database) noexcept
        : database_(database) {}

    Result<std::optional<AdminAccount>> findByUsername(
        const std::string& username) override;

    Status updateLoginState(
        const std::string& username,
        int failed_attempts,
        std::time_t locked_until) override;

    Status updatePassword(
        const std::string& username,
        const std::string& password_hash,
        const std::string& salt,
        bool must_change_password) override;

private:
    Database& database_;
};

} // namespace srms

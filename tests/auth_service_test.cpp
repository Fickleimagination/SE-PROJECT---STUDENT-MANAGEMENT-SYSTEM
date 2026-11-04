#include <gtest/gtest.h>

#include <array>
#include <ctime>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <openssl/evp.h>

#include "../src/core/auth_service.h"
#include "../src/core/console_ui.h"
using namespace srms;

namespace {

std::string toHex(const unsigned char* data, std::size_t size) {
    const char digits[] = "0123456789abcdef";
    std::string result;

    for (std::size_t i = 0; i < size; ++i) {
        result += digits[data[i] >> 4];
        result += digits[data[i] & 0x0f];
    }

    return result;
}

AdminAccount makeAccount(
    const std::string& password = "CorrectPass123",
    bool mustChange = false) {

    std::array<unsigned char, 16> salt{};
    for (std::size_t i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<unsigned char>(i + 1);
    }

    unsigned char hash[32]{};

    int ok = PKCS5_PBKDF2_HMAC(
        password.data(),
        static_cast<int>(password.size()),
        salt.data(),
        static_cast<int>(salt.size()),
        200000,
        EVP_sha256(),
        static_cast<int>(sizeof(hash)),
        hash);

    if (ok != 1) {
        throw std::runtime_error("Could not create test password hash");
    }

    return AdminAccount{
        "admin",
        toHex(hash, sizeof(hash)),
        toHex(salt.data(), salt.size()),
        0,
        0,
        mustChange
    };
}

class FakeAdminRepository : public AdminRepository {
public:
    std::optional<AdminAccount> account;
    Status loginStateResult = Status::OK;
    Status passwordResult = Status::OK;

    Result<std::optional<AdminAccount>>
    findByUsername(const std::string& username) override {
        if (!account || account->username != username) {
            return {Status::OK, std::nullopt, ""};
        }

        return {Status::OK, account, ""};
    }

    Status updateLoginState(
        const std::string& username,
        int failedAttempts,
        std::time_t lockedUntil) override {

        if (loginStateResult != Status::OK) {
            return loginStateResult;
        }

        if (!account || account->username != username) {
            return Status::NOT_FOUND;
        }

        account->failed_attempts = failedAttempts;
        account->locked_until = lockedUntil;
        return Status::OK;
    }

    Status updatePassword(
        const std::string& username,
        const std::string& passwordHash,
        const std::string& salt,
        bool mustChangePassword) override {

        if (passwordResult != Status::OK) {
            return passwordResult;
        }

        if (!account || account->username != username) {
            return Status::NOT_FOUND;
        }

        account->password_hash = passwordHash;
        account->salt = salt;
        account->must_change_password = mustChangePassword;
        return Status::OK;
    }
};

} // namespace

TEST(AuthServiceTest, EnforcesPasswordPolicy) {
    EXPECT_TRUE(AuthService::passwordMeetsPolicy("StrongPass123"));
    EXPECT_FALSE(AuthService::passwordMeetsPolicy("abc123"));
    EXPECT_FALSE(AuthService::passwordMeetsPolicy("PasswordOnly"));
    EXPECT_FALSE(
        AuthService::passwordMeetsPolicy(std::string(65, 'A') + "1"));
}

TEST(AuthServiceTest, SuccessfulLoginResetsFailedAttempts) {
    FakeAdminRepository repo;
    repo.account = makeAccount();
    repo.account->failed_attempts = 2;

    std::time_t now = 1700000000;
    AuthService service(repo, nullptr, [&now] { return now; });

    auto result = service.login("admin", "CorrectPass123");

    EXPECT_EQ(result.status, Status::OK);
    EXPECT_EQ(result.value.username, "admin");
    EXPECT_TRUE(service.isActiveAdmin(result.value));
    ASSERT_TRUE(repo.account.has_value());
    EXPECT_EQ(repo.account->failed_attempts, 0);
    EXPECT_EQ(repo.account->locked_until, 0);
}

TEST(AuthServiceTest, FiveFailedAttemptsLockAccountFor300Seconds) {
    FakeAdminRepository repo;
    repo.account = makeAccount();

    std::time_t now = 1700000000;
    AuthService service(repo, nullptr, [&now] { return now; });

    for (int i = 0; i < 5; ++i) {
        auto result = service.login("admin", "WrongPass123");
        EXPECT_EQ(result.status, Status::ACCESS_DENIED);
    }

    ASSERT_TRUE(repo.account.has_value());
    EXPECT_EQ(repo.account->failed_attempts, 5);
    EXPECT_EQ(repo.account->locked_until, now + 300);

    auto locked = service.login("admin", "CorrectPass123");
    EXPECT_EQ(locked.status, Status::ACCOUNT_LOCKED);
}

TEST(AuthServiceTest, UnknownAndWrongCredentialsUseSameMessage) {
    FakeAdminRepository repo;
    repo.account = makeAccount();

    AuthService service(repo, nullptr, [] { return 1700000000; });

    auto unknown = service.login("unknown", "WrongPass123");
    auto wrongPassword = service.login("admin", "WrongPass123");

    EXPECT_EQ(unknown.status, Status::ACCESS_DENIED);
    EXPECT_EQ(wrongPassword.status, Status::ACCESS_DENIED);
    EXPECT_EQ(unknown.detail, wrongPassword.detail);
}

TEST(AuthServiceTest, PasswordChangeStoresNewPasswordHash) {
    FakeAdminRepository repo;
    repo.account = makeAccount("InitialPass123", true);

    std::time_t now = 1700000000;
    AuthService service(repo, nullptr, [&now] { return now; });
    Session session{"admin", true, now};

    auto required = service.mustChangePassword(session);
    ASSERT_EQ(required.status, Status::OK);
    EXPECT_TRUE(required.value);

    EXPECT_EQ(
        service.changePassword(session, "weak"),
        Status::VALIDATION_ERROR);

    EXPECT_EQ(
        service.changePassword(session, "NewSecure456"),
        Status::OK);

    auto noLongerRequired = service.mustChangePassword(session);
    ASSERT_EQ(noLongerRequired.status, Status::OK);
    EXPECT_FALSE(noLongerRequired.value);

    auto login = service.login("admin", "NewSecure456");
    EXPECT_EQ(login.status, Status::OK);
}

TEST(AuthServiceTest, LogoutAndIdleTimeoutEndAdminAccess) {
    FakeAdminRepository repo;
    repo.account = makeAccount();

    std::time_t now = 1700000000;
    AuthService service(repo, nullptr, [&now] { return now; });

    Session session{"admin", true, now};
    EXPECT_TRUE(service.isActiveAdmin(session));

    Session expired{"admin", true, now - 601};
    EXPECT_FALSE(service.isActiveAdmin(expired));

    Session future{"admin", true, now + 1};
    EXPECT_FALSE(service.isActiveAdmin(future));

    service.logout(session);

    EXPECT_FALSE(session.is_admin);
    EXPECT_TRUE(session.username.empty());
    EXPECT_FALSE(service.isActiveAdmin(session));
}

TEST(ConsoleUITest, DoesNotPrintPasswordToOutput) {
    FakeAdminRepository repo;
    repo.account = makeAccount();

    std::time_t now = 1700000000;
    AuthService service(repo, nullptr, [&now] { return now; });

    std::istringstream input(
        "admin\nCorrectPass123\n3\n");
    std::ostringstream output;

    ConsoleUI ui(service, input, output);
    ui.run();

    EXPECT_NE(
        output.str().find("Login successful."),
        std::string::npos);

    EXPECT_EQ(
        output.str().find("CorrectPass123"),
        std::string::npos);
}
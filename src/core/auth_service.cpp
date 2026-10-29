#include "auth_service.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace srms {
namespace {

constexpr int kMaxFailedAttempts = 5;
constexpr std::time_t kLockoutSeconds = 300;
constexpr std::time_t kIdleLimitSeconds = 600;
constexpr int kPbkdf2Iterations = 200000;
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;

const char* kInvalidLogin =
    "ERROR: login: invalid username or password.";

std::string toHex(const unsigned char* data, std::size_t size) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);

    for (std::size_t i = 0; i < size; ++i) {
        result.push_back(digits[data[i] >> 4]);
        result.push_back(digits[data[i] & 0x0f]);
    }

    return result;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool fromHex(
    const std::string& text,
    std::vector<unsigned char>& result) {

    if (text.empty() || text.size() % 2 != 0) return false;

    result.clear();
    result.reserve(text.size() / 2);

    for (std::size_t i = 0; i < text.size(); i += 2) {
        int high = hexValue(text[i]);
        int low = hexValue(text[i + 1]);

        if (high < 0 || low < 0) {
            result.clear();
            return false;
        }

        result.push_back(
            static_cast<unsigned char>((high << 4) | low));
    }

    return true;
}

bool verifyPassword(
    const std::string& password,
    const std::string& saltHex,
    const std::string& expectedHashHex) {

    std::vector<unsigned char> salt;
    std::vector<unsigned char> expectedHash;

    if (!fromHex(saltHex, salt) ||
        !fromHex(expectedHashHex, expectedHash)) {
        return false;
    }

    if (salt.size() != kSaltBytes ||
        expectedHash.size() != kHashBytes ||
        password.size() > 64) {
        return false;
    }

    unsigned char calculated[kHashBytes]{};

    int success = PKCS5_PBKDF2_HMAC(
        password.data(),
        static_cast<int>(password.size()),
        salt.data(),
        static_cast<int>(salt.size()),
        kPbkdf2Iterations,
        EVP_sha256(),
        static_cast<int>(kHashBytes),
        calculated);

    bool matches =
        success == 1 &&
        CRYPTO_memcmp(
            calculated,
            expectedHash.data(),
            kHashBytes) == 0;

    OPENSSL_cleanse(calculated, sizeof(calculated));
    return matches;
}

bool createPasswordHash(
    const std::string& password,
    std::string& saltHex,
    std::string& hashHex) {

    unsigned char salt[kSaltBytes]{};
    unsigned char hash[kHashBytes]{};

    if (RAND_bytes(salt, static_cast<int>(sizeof(salt))) != 1) {
        return false;
    }

    int success = PKCS5_PBKDF2_HMAC(
        password.data(),
        static_cast<int>(password.size()),
        salt,
        static_cast<int>(sizeof(salt)),
        kPbkdf2Iterations,
        EVP_sha256(),
        static_cast<int>(sizeof(hash)),
        hash);

    if (success != 1) {
        OPENSSL_cleanse(salt, sizeof(salt));
        OPENSSL_cleanse(hash, sizeof(hash));
        return false;
    }

    saltHex = toHex(salt, sizeof(salt));
    hashHex = toHex(hash, sizeof(hash));

    OPENSSL_cleanse(salt, sizeof(salt));
    OPENSSL_cleanse(hash, sizeof(hash));
    return true;
}

} // namespace

AuthService::AuthService(
    AdminRepository& repository,
    AuthAuditHook audit,
    AuthClock clock)
    : repository_(repository),
      audit_(std::move(audit)),
      clock_(std::move(clock)) {

    if (!clock_) {
        clock_ = [] { return std::time(nullptr); };
    }
}

Result<Session> AuthService::login(
    const std::string& username,
    const std::string& password) {

    try {
        const std::time_t now = clock_();

        auto found = repository_.findByUsername(username);

        if (found.status != Status::OK) {
            return {found.status, Session{}, "Unable to check account."};
        }

        if (!found.value.has_value()) {
            if (audit_) {
                audit_("LOGIN_FAILURE", username, "FAILURE");
            }

            return {
                Status::ACCESS_DENIED,
                Session{},
                kInvalidLogin
            };
        }

        const AdminAccount& account = *found.value;

        if (account.locked_until > now) {
            if (audit_) {
                audit_("ACCOUNT_LOCKED", username, "DENIED");
            }

            return {
                Status::ACCOUNT_LOCKED,
                Session{},
                "ERROR: login: account locked."
            };
        }

        const bool valid = verifyPassword(
            password,
            account.salt,
            account.password_hash);

        if (!valid) {
            const int attempts = std::min(
                std::max(0, account.failed_attempts) + 1,
                kMaxFailedAttempts);

            const std::time_t lockUntil =
                attempts >= kMaxFailedAttempts
                    ? now + kLockoutSeconds
                    : 0;

            Status saved = repository_.updateLoginState(
                username, attempts, lockUntil);

            if (saved != Status::OK) {
                return {
                    saved,
                    Session{},
                    "Unable to update account state."
                };
            }

            if (audit_) {
                audit_("LOGIN_FAILURE", username, "FAILURE");

                if (attempts >= kMaxFailedAttempts) {
                    audit_("ACCOUNT_LOCKED", username, "LOCKED");
                }
            }

            return {
                Status::ACCESS_DENIED,
                Session{},
                kInvalidLogin
            };
        }

        Status reset = repository_.updateLoginState(
            username, 0, 0);

        if (reset != Status::OK) {
            return {
                reset,
                Session{},
                "Unable to reset login state."
            };
        }

        if (audit_) {
            audit_("LOGIN_SUCCESS", username, "SUCCESS");
        }

        return {
            Status::OK,
            Session{username, true, now},
            ""
        };
    } catch (...) {
        return {
            Status::INTERNAL_ERROR,
            Session{},
            "Unexpected authentication error."
        };
    }
}

Result<bool> AuthService::mustChangePassword(
    const Session& session) const {

    try {
        if (!isActiveAdmin(session)) {
            return {
                Status::ACCESS_DENIED,
                false,
                "Administrator session required."
            };
        }

        auto found = repository_.findByUsername(session.username);

        if (found.status != Status::OK) {
            return {found.status, false, "Unable to check account."};
        }

        if (!found.value.has_value()) {
            return {
                Status::ACCESS_DENIED,
                false,
                "Account unavailable."
            };
        }

        return {
            Status::OK,
            found.value->must_change_password,
            ""
        };
    } catch (...) {
        return {
            Status::INTERNAL_ERROR,
            false,
            "Unexpected authentication error."
        };
    }
}

Status AuthService::changePassword(
    Session& session,
    const std::string& newPassword) {

    try {
        if (!isActiveAdmin(session)) {
            return Status::ACCESS_DENIED;
        }

        if (!passwordMeetsPolicy(newPassword)) {
            return Status::VALIDATION_ERROR;
        }

        auto found = repository_.findByUsername(session.username);

        if (found.status != Status::OK) {
            return found.status;
        }

        if (!found.value.has_value()) {
            return Status::ACCESS_DENIED;
        }

        std::string saltHex;
        std::string hashHex;

        if (!createPasswordHash(newPassword, saltHex, hashHex)) {
            return Status::INTERNAL_ERROR;
        }

        Status updated = repository_.updatePassword(
            session.username,
            hashHex,
            saltHex,
            false);

        if (updated == Status::OK) {
            session.last_activity = clock_();
        }

        return updated;
    } catch (...) {
        return Status::INTERNAL_ERROR;
    }
}

void AuthService::logout(Session& session) {
    session = Session{"", false, 0};
}

bool AuthService::isActiveAdmin(const Session& session) const {
    if (!session.is_admin || session.username.empty()) {
        return false;
    }

    const std::time_t now = clock_();

    if (now < session.last_activity) {
        return false;
    }

    return now - session.last_activity <= kIdleLimitSeconds;
}

bool AuthService::passwordMeetsPolicy(
    const std::string& password) {

    if (password.size() < 8 || password.size() > 64) {
        return false;
    }

    bool hasLetter = false;
    bool hasDigit = false;

    for (unsigned char c : password) {
        if (std::isalpha(c)) hasLetter = true;
        if (std::isdigit(c)) hasDigit = true;
    }

    return hasLetter && hasDigit;
}

} // namespace srms
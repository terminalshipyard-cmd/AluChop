#pragma once

/**
 * @file AuthService.hpp
 * @brief Login, roles, remember-me and password recovery.
 *
 * Passwords never exist in this application as plaintext beyond the moment they are typed:
 * every credential is stored as `SHA-256(salt + password)` in hex, with a fresh 16-byte random
 * salt per user, and verification re-hashes the candidate rather than decrypting anything.
 * The same treatment applies to security answers used by the forgot-password flow.
 *
 * Internally the service throws `core::AuthException` for bad credentials and converts it to a
 * `core::Result::err` at the boundary, so the GUI branches on a value and never catches.
 */

#include <optional>

#include <QString>

#include "aluchop/core/Result.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/User.hpp"
#include "aluchop/persistence/SettingsRepository.hpp"
#include "aluchop/persistence/UserRepository.hpp"

namespace aluchop::services {

class AuditService;

/// @brief Authentication and authorisation for the whole application.
class AuthService {
public:
    AuthService(persistence::UserRepository& users,
                persistence::SettingsRepository& settings,
                AuditService& audit);

    /**
     * @brief Verifies credentials and, on success, becomes the current session.
     * @param rememberMe when true a random token is stored so the next launch logs in silently.
     * @return the logged-in user, or an error message ("Unknown user", "Wrong password", ...).
     *
     * /// @oop-concept try / catch / throw :: an internal AuthException becomes a Result::err here,
     * /// which is why no GUI code ever needs a try block
     * /// @oop-concept Default Arguments :: remember-me is opt-in
     */
    core::Result<models::User> login(const QString& username, const QString& password,
                                     bool rememberMe = false);

    /// @brief Clears the session and the stored remember-me token, and audits the logout.
    void logout();

    /// @return the signed-in user, or `std::nullopt`.
    /// @oop-concept Return by Reference :: the session is observed in place, never copied
    const std::optional<models::User>& currentUser() const noexcept { return m_current; }

    /// @return true while somebody is signed in.
    bool isLoggedIn() const noexcept { return m_current.has_value(); }

    /**
     * @brief Role gate used by every privileged operation.
     * @param atLeast minimum rank required; ranking is Admin > Manager > (Waiter == Chef).
     * @return false when nobody is signed in.
     */
    bool hasRole(models::UserRole atLeast) const;

    /// @brief Silent login from the persisted remember-me token; `std::nullopt` when absent/stale.
    std::optional<models::User> tryRememberedLogin();

    /**
     * @brief Changes the signed-in user's password.
     * @return an error when nobody is signed in, the old password is wrong, or the new one is
     *         shorter than @ref kMinPasswordLength.
     */
    core::Result<void> changePassword(const QString& oldPassword, const QString& newPassword);

    /// @return the security question stored for @p username — step 1 of the forgot-password flow.
    core::Result<QString> securityQuestionFor(const QString& username);

    /**
     * @brief Step 2 of the forgot-password flow: answer verified against its hash, password reset.
     * @return an error when the user is unknown, the answer is wrong or the password is too short.
     */
    core::Result<void> resetPasswordWithAnswer(const QString& username,
                                               const QString& answer, const QString& newPassword);

    /**
     * @brief Creates a login account. Admin-only — the check lives here, not merely in the GUI.
     * @return the new user id, or an error (not admin / duplicate username / weak password).
     */
    core::Result<int> createUser(const QString& username, const QString& password,
                                 models::UserRole role, int employeeId,
                                 const QString& securityQuestion, const QString& securityAnswer);

    /// @brief `SHA-256(salt + password)` as lowercase hex, via `QCryptographicHash`.
    /// @oop-concept Static Members :: a pure function of its inputs — no session state involved
    static QString hashPassword(const QString& password, const QString& salt);

    /// @brief 16 cryptographically random bytes as hex, from `QRandomGenerator::system()`.
    static QString generateSalt();

    /// @oop-concept Constants :: the password policy is a named constant, not a scattered literal
    inline static const int kMinPasswordLength = 6;

private:
    persistence::UserRepository& m_users;
    persistence::SettingsRepository& m_settings;
    AuditService& m_audit;
    std::optional<models::User> m_current;   ///< the active session, empty when signed out
};

} // namespace aluchop::services

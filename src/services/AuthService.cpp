/**
 * @file AuthService.cpp
 * @brief Salted SHA-256 authentication, role gating, remember-me and password recovery.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The security model in one paragraph: a password exists as plaintext only inside the QString the
 * user just typed. It is immediately folded into `SHA-256(salt || password)` and only the
 * lowercase hex digest is compared, stored or transported. Each account carries its own 16-byte
 * random salt from `QRandomGenerator::system()`, so two users who choose the same password still
 * produce different digests and one precomputed table cannot attack the whole table. Digest
 * comparison accumulates a difference over every byte instead of returning early, so the time
 * taken does not leak how much of a wrong digest happened to be right.
 *
 * The same treatment covers the security answer used by the forgot-password flow: the answer is
 * hashed with the account's salt and only the digest is kept.
 */

#include "aluchop/services/AuthService.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QLatin1String>
#include <QRandomGenerator>
#include <QtGlobal>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Logger.hpp"
#include "aluchop/services/AuditService.hpp"

namespace aluchop::services {

namespace {

/// @brief Settings key under which the current remember-me token is parked between launches.
const QString kRememberTokenKey = QStringLiteral("remember_token");

/// @brief How many random bytes a remember-me token carries (256 bits, hex-encoded).
constexpr int kRememberTokenBytes = 32;

/// @brief How many random bytes a per-user salt carries (128 bits, hex-encoded).
constexpr int kSaltBytes = 16;

/**
 * @brief Cryptographically strong random bytes as lowercase hex.
 * @param byteCount number of raw bytes to draw; the returned string is twice as long.
 *
 * `QRandomGenerator::system()` is the OS entropy source (not the seeded, reproducible generator),
 * which is the only acceptable choice for a salt or a persistent login token.
 */
QString randomHex(int byteCount) {
    QByteArray raw;
    raw.reserve(byteCount + 4);
    while (raw.size() < byteCount) {
        const quint32 chunk = QRandomGenerator::system()->generate();
        raw.append(reinterpret_cast<const char*>(&chunk), sizeof(chunk));
    }
    raw.truncate(byteCount);
    return QString::fromLatin1(raw.toHex());
}

/**
 * @brief Compares two hex digests without an early exit.
 * @return true when the two strings are byte-for-byte identical.
 *
 * A plain `a == b` stops at the first differing byte, so the time it takes reveals how long a
 * common prefix an attacker has guessed. Digests here are fixed-length hex, so comparing lengths
 * up front leaks nothing; the bytes themselves are folded together with XOR and OR so that every
 * byte is always examined.
 */
/// @oop-concept Pass by Reference :: both digests are inspected in place, never copied
bool constantTimeEquals(const QString& a, const QString& b) {
    const QByteArray x = a.toUtf8();
    const QByteArray y = b.toUtf8();
    if (x.size() != y.size() || x.isEmpty()) return false;

    unsigned char difference = 0;
    for (int i = 0; i < x.size(); ++i) {
        const auto lhs = static_cast<unsigned char>(x.at(i));
        const auto rhs = static_cast<unsigned char>(y.at(i));
        difference = static_cast<unsigned char>(difference | (lhs ^ rhs));
    }
    return difference == 0;
}

/// @brief Writes a line to `core::Logger` without letting a logging failure escape.
void quietLog(core::Logger::Level level, const QString& message) {
    try {
        core::Logger::instance().log(level, message);
    } catch (...) {
        // A lost log line must never mask the authentication outcome we are in the middle of.
    }
}

/// @brief `"user:<id>"`, the audit entity string for an account (fits the trail's 15-char field).
QString userEntity(int userId) {
    return QStringLiteral("user:%1").arg(userId);
}

/// @brief Numeric rank of a role, so "at least Manager" is a single comparison.
/// Admin outranks Manager, which outranks the floor staff; Waiter and Chef are peers.
int rankOf(models::UserRole role) {
    switch (role) {
    case models::UserRole::Admin:   return 3;
    case models::UserRole::Manager: return 2;
    case models::UserRole::Waiter:  return 1;
    case models::UserRole::Chef:    return 1;
    }
    return 0;
}

} // namespace

AuthService::AuthService(persistence::UserRepository& users,
                         persistence::SettingsRepository& settings,
                         AuditService& audit)
    : m_users(users), m_settings(settings), m_audit(audit) {
    // No session is restored here. Whether the application starts at the login window or slides
    // straight in on a remembered token is main.cpp's decision, taken through tryRememberedLogin().
}

// ---------------------------------------------------------------------------------------------
// Hashing primitives
// ---------------------------------------------------------------------------------------------

/// @oop-concept Static Members :: hashing is a pure function of its inputs, so it belongs to the
/// class rather than to any one session — SchemaMigrator seeds the admin account with this rule
QString AuthService::hashPassword(const QString& password, const QString& salt) {
    // Salt first, then password: the concatenation order is part of the stored-credential format
    // and must match the one the first-run seeder uses, or the seeded admin could never log in.
    const QByteArray material = (salt + password).toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex());
}

QString AuthService::generateSalt() {
    return randomHex(kSaltBytes);
}

// ---------------------------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------------------------

core::Result<models::User> AuthService::login(const QString& username, const QString& password,
                                              bool rememberMe) {
    const QString user = username.trimmed();
    try {
        if (user.isEmpty())
            throw core::AuthException("please enter a username");
        if (password.isEmpty())
            throw core::AuthException("please enter a password", user.toStdString());

        std::optional<models::User> found = m_users.byUsername(user);
        if (!found)
            throw core::AuthException("unknown user", user.toStdString());
        if (found->salt().isEmpty() || found->passHash().isEmpty())
            throw core::AuthException("this account has no password set", user.toStdString());

        const QString candidate = hashPassword(password, found->salt());
        if (!constantTimeEquals(candidate, found->passHash()))
            throw core::AuthException("wrong password", user.toStdString());

        // --- credentials accepted from here on -------------------------------------------------

        // Remember-me is written before the session is published so that a failure to persist the
        // token cannot leave the user logged in while believing they will be remembered.
        if (rememberMe) {
            const QString token = randomHex(kRememberTokenBytes);
            m_users.setRememberToken(found->id(), token);
            m_settings.set(kRememberTokenKey, token);
            found->setRememberToken(token);
        } else {
            m_users.setRememberToken(found->id(), QString());
            m_settings.remove(kRememberTokenKey);
            found->setRememberToken(QString());
        }

        // The audit write happens before the session is published, so an unwritable audit trail
        // refuses the login instead of granting an unrecorded one.
        m_audit.setActiveUser(found->id());
        m_audit.log(QStringLiteral("LOGIN"), userEntity(found->id()), core::Money(),
                    QStringLiteral("%1 as %2").arg(found->username(),
                                                   models::toString(found->role())));

        m_current = *found;
        return core::Result<models::User>::ok(*found);
    }
    /// @oop-concept try / catch / throw :: the AuthException raised a few lines above is caught
    /// right here and becomes a Result::err — which is why no GUI slot ever needs a try block
    catch (const core::AuthException& e) {
        m_audit.setActiveUser(m_current ? m_current->id() : 0);
        quietLog(core::Logger::Level::Warn,
                 QStringLiteral("failed login for '%1': %2").arg(user, QString::fromUtf8(e.what())));
        return core::Result<models::User>::err(QString::fromStdString(e.message()));
    }
    /// @oop-concept Multiple Catch :: a broken database is a different failure from a bad password
    /// and deserves a different message, even though both end up as the same Result kind
    catch (const core::AluChopException& e) {
        m_audit.setActiveUser(m_current ? m_current->id() : 0);
        quietLog(core::Logger::Level::Error,
                 QStringLiteral("login aborted for '%1': %2").arg(user, QString::fromUtf8(e.what())));
        return core::Result<models::User>::err(
            QStringLiteral("Could not sign in: %1").arg(QString::fromStdString(e.message())));
    }
}

void AuthService::logout() {
    if (!m_current) return;   // logging out twice is harmless, not an error

    const int userId = m_current->id();
    const QString name = m_current->username();

    // The session is cleared first and unconditionally. Even if clearing the stored token or
    // writing the audit record fails, this process must not be left holding a live session.
    m_current.reset();

    try {
        m_users.setRememberToken(userId, QString());
        m_settings.remove(kRememberTokenKey);
        m_audit.log(QStringLiteral("LOGOUT"), userEntity(userId), core::Money(), name);
    } catch (const core::AluChopException& e) {
        quietLog(core::Logger::Level::Warn,
                 QStringLiteral("logout housekeeping failed for '%1': %2")
                     .arg(name, QString::fromUtf8(e.what())));
    }

    m_audit.setActiveUser(0);   // subsequent records are stamped as system actions
}

bool AuthService::hasRole(models::UserRole atLeast) const {
    if (!m_current) return false;   // nobody signed in outranks nothing
    return rankOf(m_current->role()) >= rankOf(atLeast);
}

std::optional<models::User> AuthService::tryRememberedLogin() {
    try {
        const QString token = m_settings.get(kRememberTokenKey, QString());
        if (token.isEmpty()) return std::nullopt;

        std::optional<models::User> found = m_users.byRememberToken(token);
        if (!found) {
            // The token survived in settings but no longer matches any account — it is stale
            // (the user was deleted, or logged out on another copy of the database). Clear it so
            // the next launch does not retry a token that can never work.
            m_settings.remove(kRememberTokenKey);
            return std::nullopt;
        }

        m_audit.setActiveUser(found->id());
        m_audit.log(QStringLiteral("LOGIN_TOKEN"), userEntity(found->id()), core::Money(),
                    found->username());
        m_current = *found;
        return found;
    } catch (const core::AluChopException& e) {
        m_audit.setActiveUser(0);
        quietLog(core::Logger::Level::Warn,
                 QStringLiteral("remembered login failed: %1").arg(QString::fromUtf8(e.what())));
        return std::nullopt;   // fall back to the ordinary login window; never surface a throw
    }
}

// ---------------------------------------------------------------------------------------------
// Credential maintenance
// ---------------------------------------------------------------------------------------------

core::Result<void> AuthService::changePassword(const QString& oldPassword,
                                               const QString& newPassword) {
    try {
        if (!m_current)
            throw core::AuthException("nobody is signed in");

        const QString candidate = hashPassword(oldPassword, m_current->salt());
        if (!constantTimeEquals(candidate, m_current->passHash()))
            throw core::AuthException("the current password is not correct",
                                      m_current->username().toStdString());

        if (newPassword.length() < kMinPasswordLength)
            throw core::ValidationException(
                "the new password must be at least " + std::to_string(kMinPasswordLength) +
                    " characters",
                "newPassword");
        if (newPassword == oldPassword)
            throw core::ValidationException("the new password must differ from the old one",
                                            "newPassword");

        // The salt is intentionally *kept*. It is not only the password's salt: the stored
        // security-answer digest was computed with this very salt, and rotating it here would
        // silently destroy the forgot-password flow for an account whose answer we cannot re-hash
        // because the plaintext answer is not present at this moment.
        const QString salt = m_current->salt();
        const QString hash = hashPassword(newPassword, salt);
        m_users.setPassword(m_current->id(), hash, salt);
        m_current->setPassHash(hash);

        m_audit.log(QStringLiteral("PWD_CHANGE"), userEntity(m_current->id()), core::Money(),
                    m_current->username());
        return core::Result<void>::ok();
    } catch (const core::AluChopException& e) {
        return core::Result<void>::err(QString::fromStdString(e.message()));
    }
}

core::Result<QString> AuthService::securityQuestionFor(const QString& username) {
    try {
        const std::optional<models::User> found = m_users.byUsername(username.trimmed());
        if (!found)
            throw core::AuthException("unknown user", username.trimmed().toStdString());
        if (found->securityQuestion().isEmpty() || found->securityAnswerHash().isEmpty())
            throw core::AuthException("this account has no recovery question set",
                                      username.trimmed().toStdString());
        return core::Result<QString>::ok(found->securityQuestion());
    } catch (const core::AluChopException& e) {
        return core::Result<QString>::err(QString::fromStdString(e.message()));
    }
}

core::Result<void> AuthService::resetPasswordWithAnswer(const QString& username,
                                                        const QString& answer,
                                                        const QString& newPassword) {
    const QString user = username.trimmed();
    try {
        std::optional<models::User> found = m_users.byUsername(user);
        if (!found)
            throw core::AuthException("unknown user", user.toStdString());
        if (found->securityAnswerHash().isEmpty())
            throw core::AuthException("this account has no recovery question set", user.toStdString());
        if (newPassword.length() < kMinPasswordLength)
            throw core::ValidationException(
                "the new password must be at least " + std::to_string(kMinPasswordLength) +
                    " characters",
                "newPassword");

        // The answer is hashed exactly as typed — no trimming, no case folding. The first-run
        // seeder stores the digest of the raw answer text, so normalising here would lock the
        // seeded administrator out of their own recovery flow.
        const QString candidate = hashPassword(answer, found->salt());
        if (!constantTimeEquals(candidate, found->securityAnswerHash()))
            throw core::AuthException("that is not the right answer", user.toStdString());

        const QString hash = hashPassword(newPassword, found->salt());
        m_users.setPassword(found->id(), hash, found->salt());

        // A reset invalidates any persistent login: whoever was silently signed in on this machine
        // may not be the person who just proved they know the recovery answer.
        m_users.setRememberToken(found->id(), QString());
        m_settings.remove(kRememberTokenKey);

        m_audit.log(QStringLiteral("PWD_RESET"), userEntity(found->id()), core::Money(),
                    QStringLiteral("recovered by security question"));
        return core::Result<void>::ok();
    } catch (const core::AluChopException& e) {
        quietLog(core::Logger::Level::Warn,
                 QStringLiteral("password reset refused for '%1': %2")
                     .arg(user, QString::fromUtf8(e.what())));
        return core::Result<void>::err(QString::fromStdString(e.message()));
    }
}

core::Result<int> AuthService::createUser(const QString& username, const QString& password,
                                          models::UserRole role, int employeeId,
                                          const QString& securityQuestion,
                                          const QString& securityAnswer) {
    const QString user = username.trimmed();
    try {
        // The privilege check lives here, in the service, so that *no* caller — a future GUI page,
        // a test, a command-line tool — can create an account by simply not asking the question.
        if (!hasRole(models::UserRole::Admin))
            throw core::AuthException("only an administrator may create login accounts",
                                      "createUser");

        if (user.isEmpty())
            throw core::ValidationException("the username must not be blank", "username");
        if (user.contains(QLatin1Char(' ')))
            throw core::ValidationException("the username must not contain spaces", "username");
        if (password.length() < kMinPasswordLength)
            throw core::ValidationException(
                "the password must be at least " + std::to_string(kMinPasswordLength) +
                    " characters",
                "password");
        if (m_users.byUsername(user))
            throw core::ValidationException("that username is already taken", "username");

        // One salt per account, drawn fresh from the system entropy pool, and used for both the
        // password digest and the recovery-answer digest.
        const QString salt = generateSalt();

        models::User account(0, user, role);
        account.setSalt(salt);
        account.setPassHash(hashPassword(password, salt));
        account.setEmployeeId(employeeId);
        account.setSecurityQuestion(securityQuestion.trimmed());
        account.setSecurityAnswerHash(
            securityAnswer.isEmpty() ? QString() : hashPassword(securityAnswer, salt));
        account.setRememberToken(QString());

        const int newId = m_users.insert(account);
        m_audit.log(QStringLiteral("USER_CREATE"), userEntity(newId), core::Money(),
                    QStringLiteral("%1 as %2").arg(user, models::toString(role)));
        return core::Result<int>::ok(newId);
    } catch (const core::AluChopException& e) {
        return core::Result<int>::err(QString::fromStdString(e.message()));
    }
}

} // namespace aluchop::services

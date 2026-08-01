#pragma once

/// \file
/// \brief User — a login account.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QDateTime>
#include <QString>

#include "aluchop/models/Enums.hpp"

namespace aluchop::models {

/// \brief Credentials and authorisation data for one login.
///
/// A User is *not* a Person. It is deliberately outside the people hierarchy:
/// a login is a credential, not a human, and the two have different lifetimes —
/// an employee can leave while their account is disabled but retained for audit.
/// The optional link to a person is a plain foreign key in employeeId().
///
/// \warning The class stores a **salted SHA-256 digest**, never a password.
///          AuthService owns the hashing; nothing here ever sees plaintext.
class User {
public:
    /// \brief Default constructor — used by UserRepository before hydration.
    User() = default;

    /// @oop-concept Parameterised Constructor :: identity + authorisation in one step
    /// \param id       Database primary key, 0 when not yet persisted.
    /// \param username Login name; must be unique and non-empty.
    /// \param role     Authorisation role.
    User(int id, QString username, UserRole role);

    /// \brief Database primary key.
    int id() const noexcept { return m_id; }
    /// \brief Assign the primary key after an INSERT.
    void setId(int v) { m_id = v; }

    /// \brief Login name.
    const QString& username() const noexcept { return m_username; }
    /// \brief Set the login name.
    /// \throws core::ValidationException when \p v trims to empty.
    void setUsername(const QString& v);

    /// \brief Authorisation role; drives every permission check in the GUI.
    UserRole role() const noexcept { return m_role; }
    /// \brief Set the authorisation role.
    void setRole(UserRole r) { m_role = r; }

    /// \brief Hex-encoded SHA-256 of (salt + password).
    const QString& passHash() const noexcept { return m_passHash; }
    /// \brief Set the password digest (AuthService only).
    void setPassHash(const QString& v) { m_passHash = v; }

    /// \brief Per-user random salt, hex-encoded.
    const QString& salt() const noexcept { return m_salt; }
    /// \brief Set the salt (AuthService only).
    void setSalt(const QString& v) { m_salt = v; }

    /// \brief Linked employee row, or 0 when the account has no staff record.
    int employeeId() const noexcept { return m_employeeId; }
    /// \brief Link this account to an employee.
    void setEmployeeId(int v) { m_employeeId = v; }

    /// \brief Prompt shown by the forgot-password flow.
    const QString& securityQuestion() const noexcept { return m_securityQuestion; }
    /// \brief Set the forgot-password prompt.
    void setSecurityQuestion(const QString& v) { m_securityQuestion = v; }

    /// \brief Salted digest of the security answer — the answer is never stored.
    const QString& securityAnswerHash() const noexcept { return m_securityAnswerHash; }
    /// \brief Set the security-answer digest (AuthService only).
    void setSecurityAnswerHash(const QString& v) { m_securityAnswerHash = v; }

    /// \brief Opaque "remember me" token; empty when the feature is off.
    const QString& rememberToken() const noexcept { return m_rememberToken; }
    /// \brief Set or clear the remember-me token.
    void setRememberToken(const QString& v) { m_rememberToken = v; }

private:
    int m_id = 0;                          ///< Primary key.
    QString m_username;                    ///< Unique login name.
    UserRole m_role = UserRole::Waiter;    ///< Least-privilege default.
    QString m_passHash;                    ///< SHA-256(salt + password), hex.
    QString m_salt;                        ///< Per-user random salt, hex.
    int m_employeeId = 0;                  ///< 0 = no linked employee.
    QString m_securityQuestion;            ///< Forgot-password prompt.
    QString m_securityAnswerHash;          ///< SHA-256(salt + answer), hex.
    QString m_rememberToken;               ///< Persistent-login token.
};

} // namespace aluchop::models

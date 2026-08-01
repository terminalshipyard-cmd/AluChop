/// \file
/// \brief Implementation of User — a login account.
///
/// User is intentionally outside the Person hierarchy: a credential is not a
/// human. Nothing in this file ever sees, stores or compares a plaintext
/// password — AuthService owns the salted SHA-256 hashing and only ever hands
/// this class the resulting digest.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/User.hpp"

#include <utility>

#include <QChar>
#include <QString>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::models {

/// @oop-concept Parameterised Constructor :: identity + authorisation in one step
User::User(int id, QString username, UserRole role)
    : m_id(id), m_role(role)
{
    setUsername(username); // one validation rule, used by construction and editing alike
}

void User::setUsername(const QString& v)
{
    const QString trimmed = v.trimmed();
    if (trimmed.isEmpty())
        throw core::ValidationException("username must not be blank");

    // A login name is a lookup key that is typed by hand at every sign-in, so
    // embedded whitespace is rejected rather than silently accepted and then
    // failing to match the stored row.
    for (const QChar c : trimmed) {
        if (c.isSpace())
            throw core::ValidationException("username must not contain spaces: '"
                                            + trimmed.toStdString() + "'");
    }

    m_username = trimmed;
}

} // namespace aluchop::models

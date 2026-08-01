#pragma once

/**
 * @file UserRepository.hpp
 * @brief CRUD and lookups for the `users` table (login accounts).
 *
 * Only hashes and salts ever cross this boundary — plaintext passwords exist nowhere in the
 * persistence layer. Hashing is `services::AuthService`'s job.
 */

#include <optional>

#include <QSqlRecord>
#include <QString>

#include "aluchop/models/User.hpp"
#include "aluchop/persistence/Repository.hpp"

namespace aluchop::persistence {

/**
 * @brief Repository over the `users` table.
 * /// @oop-concept Public Inheritance :: a UserRepository IS-A Repository of users
 */
class UserRepository : public Repository<models::User> {
public:
    UserRepository();

    /// @brief Inserts a new account. @return the generated primary key.
    /// @throws core::DatabaseException on constraint violation (duplicate username).
    int insert(const models::User& u);

    /// @brief Rewrites every mutable column of an existing account.
    void update(const models::User& u);

    /// @return the account with this exact username, or `std::nullopt`.
    std::optional<models::User> byUsername(const QString& username) const;

    /// @return the account whose remember-me token matches, or `std::nullopt` (empty token never matches).
    std::optional<models::User> byRememberToken(const QString& token) const;

    /// @brief Stores a remember-me token; an empty @p token clears it (logout / "forget me").
    void setRememberToken(int userId, const QString& token);

    /// @brief Replaces the stored credential pair after a password change or reset.
    void setPassword(int userId, const QString& hash, const QString& salt);

protected:
    /// @oop-concept Method Overriding :: users hydrate role, salt/hash and recovery columns
    models::User fromRecord(const QSqlRecord& rec) const override;
};

} // namespace aluchop::persistence

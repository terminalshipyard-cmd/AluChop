/**
 * @file UserRepository.cpp
 * @brief CRUD and lookups for the `users` table (login accounts).
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Nothing in this file has any idea what a password looks like. It moves an opaque hex digest and an
 * opaque hex salt between `models::User` and the `users` table; producing and verifying them is
 * `services::AuthService`'s job. That separation is what guarantees no plaintext credential can ever
 * reach SQL, however carelessly a caller behaves.
 */

#include "aluchop/persistence/UserRepository.hpp"

#include <QSqlQuery>
#include <QVariant>

#include "aluchop/models/Enums.hpp"
#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {
namespace {

/// `users.employee_id` is a nullable foreign key: "no linked staff record" is NULL, never 0.
/// Binding 0 would be rejected outright, because `PRAGMA foreign_keys` is ON and no employee has id 0.
QVariant fkOrNull(int id) { return id > 0 ? QVariant(id) : QVariant(); }

} // namespace

UserRepository::UserRepository() : Repository(QStringLiteral("users")) {}

int UserRepository::insert(const models::User& u) {
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("INSERT INTO users "
                       "(username, pass_hash, salt, role, employee_id, "
                       " security_question, security_answer_hash, remember_token) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"),
        { u.username(), u.passHash(), u.salt(), models::toString(u.role()),
          fkOrNull(u.employeeId()), u.securityQuestion(), u.securityAnswerHash(),
          u.rememberToken() });
    return q.lastInsertId().toInt();
}

void UserRepository::update(const models::User& u) {
    // Every mutable column is rewritten, so callers must edit a *hydrated* account rather than a
    // freshly built one — otherwise an empty digest would legitimately overwrite the stored one.
    Database::instance().prepared(
        QStringLiteral("UPDATE users SET username = ?, pass_hash = ?, salt = ?, role = ?, "
                       "employee_id = ?, security_question = ?, security_answer_hash = ?, "
                       "remember_token = ? WHERE id = ?"),
        { u.username(), u.passHash(), u.salt(), models::toString(u.role()),
          fkOrNull(u.employeeId()), u.securityQuestion(), u.securityAnswerHash(),
          u.rememberToken(), u.id() });
}

std::optional<models::User> UserRepository::byUsername(const QString& username) const {
    if (username.isEmpty()) return std::nullopt;
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM users WHERE username = ? LIMIT 1"), { username });
    if (q.next()) return fromRecord(q.record());
    return std::nullopt;
}

std::optional<models::User> UserRepository::byRememberToken(const QString& token) const {
    // An empty token is the schema's "remember-me is off" marker. Letting it match would sign in the
    // first account that had ever cleared its token, so it is rejected before SQL is even reached.
    if (token.isEmpty()) return std::nullopt;
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM users WHERE remember_token = ? AND remember_token <> '' LIMIT 1"),
        { token });
    if (q.next()) return fromRecord(q.record());
    return std::nullopt;
}

void UserRepository::setRememberToken(int userId, const QString& token) {
    Database::instance().prepared(
        QStringLiteral("UPDATE users SET remember_token = ? WHERE id = ?"), { token, userId });
}

void UserRepository::setPassword(int userId, const QString& hash, const QString& salt) {
    // Hash and salt move together: a digest is meaningless beside the wrong salt, so they are never
    // written by two separate statements.
    Database::instance().prepared(
        QStringLiteral("UPDATE users SET pass_hash = ?, salt = ? WHERE id = ?"),
        { hash, salt, userId });
}

models::User UserRepository::fromRecord(const QSqlRecord& rec) const {
    models::User u(rec.value(QStringLiteral("id")).toInt(),
                   rec.value(QStringLiteral("username")).toString(),
                   models::userRoleFromString(rec.value(QStringLiteral("role")).toString()));
    u.setPassHash(rec.value(QStringLiteral("pass_hash")).toString());
    u.setSalt(rec.value(QStringLiteral("salt")).toString());
    u.setEmployeeId(rec.value(QStringLiteral("employee_id")).toInt());
    u.setSecurityQuestion(rec.value(QStringLiteral("security_question")).toString());
    u.setSecurityAnswerHash(rec.value(QStringLiteral("security_answer_hash")).toString());
    u.setRememberToken(rec.value(QStringLiteral("remember_token")).toString());
    return u;
}

} // namespace aluchop::persistence

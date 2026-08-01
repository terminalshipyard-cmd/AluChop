/**
 * @file CustomerRepository.cpp
 * @brief CRUD and lookups for the `customers` table (the loyalty database).
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include "aluchop/persistence/CustomerRepository.hpp"

#include <QDateTime>
#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>

#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {
namespace {

constexpr auto kTsFormat = "yyyy-MM-ddTHH:mm:ss";

QString tsToDb(const QDateTime& dt) {
    return dt.toUTC().toString(QString::fromLatin1(kTsFormat));
}

QDateTime tsFromDb(const QString& s) {
    QDateTime dt = QDateTime::fromString(s, QString::fromLatin1(kTsFormat));
    if (!dt.isValid()) dt = QDateTime::fromString(s, Qt::ISODate);
    if (dt.isValid()) dt.setTimeZone(QTimeZone::UTC);
    return dt;
}

/// `customers.phone` is UNIQUE. Several guests legitimately have no number on file, and in SQLite
/// several NULLs never collide while several empty strings would — so "no phone" is stored as NULL.
QVariant phoneOrNull(const QString& phone) {
    return phone.isEmpty() ? QVariant() : QVariant(phone);
}

/// Escapes the LIKE wildcards so a customer searching for "100%" does not match every row.
QString likePattern(const QString& term) {
    QString t = term;
    t.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    t.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    t.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    QString p;
    p.reserve(t.size() + 2);
    p += QLatin1Char('%');
    p += t;
    p += QLatin1Char('%');
    return p;
}

} // namespace

CustomerRepository::CustomerRepository() : Repository(QStringLiteral("customers")) {}

int CustomerRepository::insert(const models::Customer& c) {
    const QDateTime created = c.createdAt().isValid() ? c.createdAt() : QDateTime::currentDateTimeUtc();
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("INSERT INTO customers (name, phone, email, loyalty_points, visits, created_at) "
                       "VALUES (?, ?, ?, ?, ?, ?)"),
        { c.name(), phoneOrNull(c.phone()), c.email(), c.loyaltyPoints(), c.visits(),
          tsToDb(created) });
    return q.lastInsertId().toInt();
}

void CustomerRepository::update(const models::Customer& c) {
    // Loyalty points and visits ride along with the identity fields: `++customer` followed by
    // update() is the whole of "record a visit", with no second statement to forget.
    Database::instance().prepared(
        QStringLiteral("UPDATE customers SET name = ?, phone = ?, email = ?, "
                       "loyalty_points = ?, visits = ? WHERE id = ?"),
        { c.name(), phoneOrNull(c.phone()), c.email(), c.loyaltyPoints(), c.visits(), c.id() });
}

std::optional<models::Customer> CustomerRepository::byPhone(const QString& phone) const {
    if (phone.isEmpty()) return std::nullopt;
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM customers WHERE phone = ? LIMIT 1"), { phone });
    if (q.next()) return fromRecord(q.record());
    return std::nullopt;
}

std::vector<models::Customer> CustomerRepository::search(const QString& term) const {
    std::vector<models::Customer> out;
    const QString pattern = likePattern(term.trimmed());
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM customers "
                       "WHERE name LIKE ? ESCAPE '\\' OR phone LIKE ? ESCAPE '\\' "
                       "ORDER BY name"),
        { pattern, pattern });
    while (q.next()) out.push_back(fromRecord(q.record()));
    return out;
}

models::Customer CustomerRepository::fromRecord(const QSqlRecord& rec) const {
    models::Customer c(rec.value(QStringLiteral("id")).toInt(),
                       rec.value(QStringLiteral("name")).toString(),
                       rec.value(QStringLiteral("phone")).toString(),
                       rec.value(QStringLiteral("email")).toString());
    // The counters are restored, not replayed: setLoyaltyPoints/setVisits exist precisely so
    // hydration does not have to call addLoyaltyPoints() or `++c` hundreds of times.
    c.setLoyaltyPoints(rec.value(QStringLiteral("loyalty_points")).toInt());
    c.setVisits(rec.value(QStringLiteral("visits")).toInt());
    c.setCreatedAt(tsFromDb(rec.value(QStringLiteral("created_at")).toString()));
    return c;
}

QString CustomerRepository::orderByClause() const { return QStringLiteral("name"); }

} // namespace aluchop::persistence

/**
 * @file SettingsRepository.cpp
 * @brief Key/value access to the `settings` table.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * `settings` is the one table in the schema that is not keyed by an integer id, which is why this
 * class deliberately stands outside `Repository<T>` (see the header). Every statement below is
 * prepared with bound parameters, so a key or value containing a quote is data, never syntax.
 */

#include "aluchop/persistence/SettingsRepository.hpp"

#include <QSqlQuery>
#include <QVariant>

#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {

QString SettingsRepository::get(const QString& key, const QString& fallback) const {
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT value FROM settings WHERE key = ?"), { key });
    if (q.next()) return q.value(0).toString();
    return fallback;
}

void SettingsRepository::set(const QString& key, const QString& value) {
    // Single-statement UPSERT: the primary key decides whether this is an insert or an update, so
    // there is no read-then-write window in which another writer could slip between the two.
    Database::instance().prepared(
        QStringLiteral("INSERT INTO settings (key, value) VALUES (?, ?) "
                       "ON CONFLICT(key) DO UPDATE SET value = excluded.value"),
        { key, value });
}

void SettingsRepository::remove(const QString& key) {
    Database::instance().prepared(QStringLiteral("DELETE FROM settings WHERE key = ?"), { key });
}

} // namespace aluchop::persistence

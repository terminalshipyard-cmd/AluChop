/**
 * @file SettingsService.cpp
 * @brief Restaurant preferences with a write-through cache, plus backup/restore orchestration.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Settings are read far more often than they are written — the theme on every window that opens,
 * the service-charge percentage on every bill, the restaurant name on every receipt — so a
 * `std::map` sits in front of the `settings` table. The cache is `mutable` because reading a
 * setting is *logically* const: the value a caller observes is identical whether or not the read
 * happened to populate the cache, so the constness of get() is honest rather than a loophole.
 *
 * Every write goes to the database first and to the cache second, so a failed write leaves the
 * cache agreeing with what is actually stored rather than with what somebody hoped to store.
 */

#include "aluchop/services/SettingsService.hpp"

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/services/AuditService.hpp"

namespace aluchop::services {

namespace {

/**
 * @brief Whether a settings key holds a secret that must never reach the audit trail or a log.
 *
 * The remember-me token lives in this table. Auditing its value would defeat the point of hashing
 * everything else, so the audit record for such a key names the key and nothing more.
 */
bool isSecretKey(const QString& key) {
    return key.contains(QStringLiteral("token"), Qt::CaseInsensitive)
        || key.contains(QStringLiteral("password"), Qt::CaseInsensitive)
        || key.contains(QStringLiteral("secret"), Qt::CaseInsensitive);
}

} // namespace

SettingsService::SettingsService(persistence::SettingsRepository& repo,
                                 persistence::BackupManager& backups, AuditService& audit)
    : m_repo(repo), m_backups(backups), m_audit(audit) {
    // The cache starts empty and fills lazily. Pre-loading the whole table would be pointless work
    // at start-up for a table that is read a few dozen times in a session.
}

// ---------------------------------------------------------------------------------------------
// Key/value access
// ---------------------------------------------------------------------------------------------

/// @oop-concept Constant Member Functions :: reading a setting is logically const, which is
/// exactly why the cache that makes it fast is declared `mutable`
QString SettingsService::get(const QString& key, const QString& fallback) const {
    /// @oop-concept STL (map) :: an ordered key/value cache is precisely what std::map is for
    const auto cached = m_cache.find(key);
    if (cached != m_cache.end()) return cached->second;

    QString stored;
    try {
        // Ask with an *empty* fallback so that "absent" is distinguishable from "the caller's
        // default". Caching the caller's fallback would be wrong: the next caller may pass a
        // different one, and the key still does not exist.
        stored = m_repo.get(key, QString());
    } catch (const core::AluChopException&) {
        return fallback;   // an unreadable settings table must not stop a window from opening
    }

    if (stored.isEmpty()) return fallback;   // absent — deliberately not cached

    m_cache.emplace(key, stored);
    return stored;
}

void SettingsService::set(const QString& key, const QString& value) {
    const QString cleanKey = key.trimmed();
    if (cleanKey.isEmpty()) return;   // a nameless setting is not a setting

    try {
        m_repo.set(cleanKey, value);   // database first...
        m_cache[cleanKey] = value;     // ...cache second, so the two can never disagree

        m_audit.log(QStringLiteral("SETTING_SET"), QStringLiteral("setting"), core::Money(),
                    isSecretKey(cleanKey) ? cleanKey + QStringLiteral(" = (hidden)")
                                          : cleanKey + QStringLiteral(" = ") + value);
    } catch (const core::AluChopException&) {
        // A setting that could not be stored must not linger in the cache pretending it was.
        m_cache.erase(cleanKey);
    }
}

// ---------------------------------------------------------------------------------------------
// Backup / restore
// ---------------------------------------------------------------------------------------------

core::Result<QString> SettingsService::createBackup() {
    try {
        const QString path = m_backups.createBackup();

        m_audit.log(QStringLiteral("BACKUP"), QStringLiteral("database"), core::Money(), path);
        return core::Result<QString>::ok(path);
    }
    /// @oop-concept Multiple Catch :: a file-system failure and a database failure are different
    /// accidents and deserve different sentences, even though both end as the same Result kind
    catch (const core::FileIOException& e) {
        return core::Result<QString>::err(
            QStringLiteral("Backup failed: %1").arg(QString::fromStdString(e.message())));
    } catch (const core::AluChopException& e) {
        return core::Result<QString>::err(
            QStringLiteral("Backup failed: %1").arg(QString::fromStdString(e.message())));
    }
}

core::Result<void> SettingsService::restoreBackup(const QString& path) {
    try {
        if (path.trimmed().isEmpty())
            throw core::ValidationException("no backup file was selected", "path");

        // A restore is destructive, so the candidate is proved to be a real SQLite database by
        // reading its 16-byte header before anything is overwritten — the extension is not trusted.
        if (!persistence::BackupManager::isValidSqliteFile(path))
            throw core::FileIOException("that file is not a valid AluChop database",
                                        path.toStdString());

        m_backups.restoreBackup(path);

        // Every cached value now describes the *previous* database. Dropping the whole cache is
        // the only correct move; repopulating it lazily costs one query per key actually needed.
        m_cache.clear();

        m_audit.log(QStringLiteral("RESTORE"), QStringLiteral("database"), core::Money(), path);
        return core::Result<void>::ok();
    } catch (const core::AluChopException& e) {
        return core::Result<void>::err(
            QStringLiteral("Restore failed: %1").arg(QString::fromStdString(e.message())));
    }
}

std::vector<QString> SettingsService::listBackups() const {
    try {
        return m_backups.listBackups();   // newest first
    } catch (const core::AluChopException&) {
        return {};   // an unreadable backup directory simply shows as "no backups yet"
    }
}

} // namespace aluchop::services

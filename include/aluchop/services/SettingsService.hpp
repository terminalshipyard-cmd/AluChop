#pragma once

/**
 * @file SettingsService.hpp
 * @brief Restaurant preferences plus backup, restore and export orchestration.
 *
 * Settings are read constantly (the theme on every window, the service-charge percentage on every
 * bill), so the service keeps a small in-memory cache in front of the table and writes through it
 * on every change.
 */

#include <map>
#include <vector>

#include <QString>

#include "aluchop/core/Result.hpp"
#include "aluchop/persistence/BackupManager.hpp"
#include "aluchop/persistence/SettingsRepository.hpp"

namespace aluchop::services {

class AuditService;

/**
 * @brief Key/value preferences with a write-through cache.
 *
 * Well-known keys: `restaurant.name`, `restaurant.address`, `restaurant.phone`,
 * `theme.mode` (`light` / `dark`), `billing.service_charge_pct`.
 */
class SettingsService {
public:
    SettingsService(persistence::SettingsRepository& repo,
                    persistence::BackupManager& backups, AuditService& audit);

    /**
     * @brief Reads a setting, consulting the cache first.
     * /// @oop-concept Constant Member Functions :: reading a setting is logically const, which is
     * /// exactly why the cache is `mutable` — the observable value never changes
     * /// @oop-concept Default Arguments :: an unset key yields the caller's fallback
     */
    QString get(const QString& key, const QString& fallback = QString()) const;

    /// @brief Writes a setting to the database and updates the cache.
    void set(const QString& key, const QString& value);

    /// @brief Takes a timestamped database snapshot. @return the backup path, or an error.
    core::Result<QString> createBackup();

    /// @brief Restores a previously created snapshot after validating it is a real SQLite file.
    core::Result<void> restoreBackup(const QString& path);

    /// @return available backups, newest first.
    std::vector<QString> listBackups() const;

private:
    persistence::SettingsRepository& m_repo;
    persistence::BackupManager& m_backups;
    AuditService& m_audit;

    /// @oop-concept STL (map) :: an ordered key/value cache is precisely what std::map is for
    mutable std::map<QString, QString> m_cache;
};

} // namespace aluchop::services

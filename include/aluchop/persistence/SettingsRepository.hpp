#pragma once

/**
 * @file SettingsRepository.hpp
 * @brief Key/value access to the `settings` table.
 *
 * Deliberately **not** a `Repository<T>`: `settings` is keyed by a TEXT key, not by an integer id,
 * so the generic id-based CRUD skeleton simply does not apply. Forcing it in would be a worse fit
 * than writing the four honest methods below.
 */

#include <QString>

namespace aluchop::persistence {

/// @brief Small key/value store used for schema version, theme, restaurant info and tokens.
class SettingsRepository {
public:
    SettingsRepository() = default;

    /**
     * @brief Reads one setting.
     * /// @oop-concept Default Arguments :: a missing key yields the caller's fallback, not an error
     */
    QString get(const QString& key, const QString& fallback = QString()) const;

    /// @brief Inserts or replaces one setting (UPSERT on the primary key).
    void set(const QString& key, const QString& value);

    /// @brief Deletes one setting; a no-op when the key is absent.
    void remove(const QString& key);
};

} // namespace aluchop::persistence

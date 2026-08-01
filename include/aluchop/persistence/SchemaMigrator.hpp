#pragma once

/**
 * @file SchemaMigrator.hpp
 * @brief Versioned DDL migrations plus first-run seeding of the AluChop database.
 *
 * The schema version lives in `settings['schema_version']` (absent == 0). Each migration
 * `n -> n+1` runs inside `Database::transaction`, so a failed migration leaves the database
 * exactly as it was. Shipped DDL is immutable: future schema changes append `applyMigration2()`,
 * they never edit `applyMigration1()`.
 */

#include <QString>

#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {

/**
 * @brief Brings an empty or older database up to @ref kLatestVersion and seeds it once.
 *
 * Seeding order (docs/ARCHITECTURE.md §6): menu items from JSON, the admin employee + user,
 * tables T1..T12, suppliers, ingredients, per-item recipes and the two launch promo codes.
 */
class SchemaMigrator {
public:
    /**
     * @param db the already-open database to migrate.
     * /// @oop-concept Parameterised Constructor :: a migrator is meaningless without its database
     */
    explicit SchemaMigrator(Database& db);

    /**
     * @brief Applies every pending migration and, on a first run, seeds reference data.
     * @param menuSeedJsonPath path to `assets/menu/menu_seed.json`.
     * @throws core::DatabaseException when DDL fails.
     * @throws core::ValidationException when the seed JSON is missing or malformed.
     */
    void migrate(const QString& menuSeedJsonPath);

    /// @return the stored schema version, 0 when the settings row does not exist yet.
    /// @oop-concept Constant Member Functions :: an observer must not mutate the database object
    int currentVersion() const;

    /// @oop-concept Constants :: the target schema version, never a magic literal in the code
    static constexpr int kLatestVersion = 1;

private:
    /// Full DDL of docs/ARCHITECTURE.md §6, executed inside one transaction.
    void applyMigration1();

    /**
     * @brief Populates reference data exactly once (skipped when `menu_items` already has rows).
     * @throws core::ValidationException if the menu seed file cannot be parsed.
     */
    void seedIfEmpty(const QString& menuSeedJsonPath);

    Database& m_db;   ///< borrowed, never owned
};

} // namespace aluchop::persistence

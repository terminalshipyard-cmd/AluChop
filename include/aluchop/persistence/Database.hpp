#pragma once

/**
 * @file Database.hpp
 * @brief Owns the one and only SQLite connection used by AluChop.
 *
 * The whole persistence layer talks to SQLite through this class. It is the only place that knows
 * the driver name (`QSQLITE`), the connection name (`aluchop_main`) and the on-disk file path.
 * Repositories borrow the live handle by reference and issue statements through @ref Database::exec
 * and @ref Database::prepared, both of which convert a failed `QSqlQuery` into a
 * `core::DatabaseException` so no caller can silently ignore a broken statement.
 *
 * Threading (docs/ARCHITECTURE.md §9): the application is single-threaded, so the per-thread
 * `QSqlDatabase` restriction never triggers.
 */

#include <functional>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariantList>

namespace aluchop::persistence {

/**
 * @brief Process-wide SQLite connection with RAII transaction support.
 *
 * @note Prices/amounts are stored as INTEGER paisa columns — never REAL. See docs/ARCHITECTURE.md §6.
 *
 * /// @oop-concept Static Members :: exactly one DB connection, owned by a Meyers singleton
 */
class Database {
public:
    /// Function-local static instance; constructed on first use, destroyed at exit.
    static Database& instance();

    /**
     * @brief Opens (or creates) the SQLite database file and enables foreign keys.
     * @param dbFilePath absolute path to `aluchop.db`.
     * @throws core::DatabaseException when the driver is missing or the file cannot be opened.
     * @post `PRAGMA foreign_keys = ON` has been executed on the connection.
     */
    void open(const QString& dbFilePath);

    /// Closes and removes the named connection. Safe to call when already closed.
    void close();

    /// @return true while the underlying `QSqlDatabase` is open.
    bool isOpen() const;

    /// @return the path passed to open(), empty before the first open().
    QString filePath() const { return m_path; }

    /**
     * @brief Live connection handle.
     * /// @oop-concept Return by Reference :: repositories borrow the live handle, never copy it
     */
    QSqlDatabase& handle();

    /**
     * @brief Executes a statement with no bound values.
     * @throws core::DatabaseException carrying the driver text and the offending SQL.
     */
    QSqlQuery exec(const QString& sql);

    /**
     * @brief Prepares @p sql, binds @p binds positionally to the `?` placeholders and executes.
     * @throws core::DatabaseException on prepare or execute failure.
     */
    QSqlQuery prepared(const QString& sql, const QVariantList& binds);

    /**
     * @brief Runs @p body inside a single SQL transaction (RAII-style all-or-nothing guard).
     *
     * Commits when @p body returns normally. If @p body throws anything at all the transaction is
     * rolled back first and the original exception continues to propagate untouched.
     *
     * /// @oop-concept Rethrow :: rolls back on ANY exception, then `throw;` so callers still see it
     * /// @oop-concept Pass by Reference :: the work unit is taken as a const std::function reference
     *
     * @throws core::DatabaseException when the transaction cannot be started or committed.
     */
    void transaction(const std::function<void()>& body);

    /// A connection is a unique resource — copying the singleton would create a second owner.
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

private:
    Database() = default;

    /// @oop-concept Destructor :: closes the connection when the process shuts down (RAII)
    ~Database();

    QString m_path;      ///< path of the currently open database file
    QSqlDatabase m_db;   ///< named connection "aluchop_main"
};

} // namespace aluchop::persistence

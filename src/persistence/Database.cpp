/**
 * @file Database.cpp
 * @brief Implementation of the process-wide SQLite connection (docs/ARCHITECTURE.md §3.3, §9).
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Only this file knows the driver name, the connection name and how a failed `QSqlQuery` becomes a
 * `core::DatabaseException`. Everything above it (repositories, services, GUI) sees SQL results or
 * an exception — never a silently-ignored error.
 */

#include "aluchop/persistence/Database.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QStringList>
#include <QVariant>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::persistence {
namespace {

/// @oop-concept Constants :: the driver and connection names exist exactly once in the program
const QString kDriverName     = QStringLiteral("QSQLITE");
const QString kConnectionName = QStringLiteral("aluchop_main");

/**
 * @brief Increments a counter for as long as it is alive.
 *
 * SQLite has no nested `BEGIN`, so a `transaction()` invoked from inside another `transaction()`
 * must join the outer unit of work instead of starting a second one. This tiny guard is what makes
 * that nesting depth exception-safe: it is decremented by stack unwinding, not by hand.
 *
 * /// @oop-concept Destructor :: the depth is restored even when the body throws (RAII)
 */
struct DepthGuard {
    explicit DepthGuard(int& counter) noexcept : m_counter(counter) { ++m_counter; }
    ~DepthGuard() { --m_counter; }

    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;

private:
    int& m_counter;
};

/// @brief Folds a QSqlError into the message/context/code triple carried by DatabaseException.
[[noreturn]] void throwSqlError(const QString& what, const QSqlError& error, const QString& context)
{
    const QString message = error.text().trimmed().isEmpty()
                                ? what
                                : what + QStringLiteral(": ") + error.text().trimmed();
    throw core::DatabaseException(message.toStdString(), context.toStdString(),
                                  error.nativeErrorCode().toInt());
}

} // namespace

Database& Database::instance()
{
    // Function-local static: constructed on first use, destroyed at exit (ARCHITECTURE §9 rule 6).
    static Database s_instance;
    return s_instance;
}

Database::~Database()
{
    // This singleton is a function-local static, so it is destroyed *after* QCoreApplication has
    // already gone. Touching the QSqlDatabase registry at that point only makes Qt log
    // "QSqlDatabase requires a QCoreApplication"; the driver has released everything by then.
    if (QCoreApplication::instance() == nullptr) {
        return;
    }
    // A destructor must never throw; close() cannot, but the guard documents the intent.
    try {
        close();
    } catch (...) {
        // Swallowed deliberately: the process is going away regardless.
    }
}

void Database::open(const QString& dbFilePath)
{
    if (dbFilePath.trimmed().isEmpty()) {
        throw core::DatabaseException("database path is empty", "Database::open");
    }
    if (isOpen() && m_path == dbFilePath) {
        return; // already serving this exact file — opening twice would be a no-op anyway
    }
    if (isOpen()) {
        close();
    }

    if (!QSqlDatabase::isDriverAvailable(kDriverName)) {
        throw core::DatabaseException("the QSQLITE driver is not available in this Qt build",
                                      QSqlDatabase::drivers().join(QLatin1Char(',')).toStdString());
    }

    const QFileInfo info(dbFilePath);
    const QDir parent = info.absoluteDir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        throw core::DatabaseException("cannot create the database directory",
                                      parent.absolutePath().toStdString());
    }

    m_db = QSqlDatabase::contains(kConnectionName)
               ? QSqlDatabase::database(kConnectionName, /*open=*/false)
               : QSqlDatabase::addDatabase(kDriverName, kConnectionName);
    m_db.setDatabaseName(info.absoluteFilePath());

    if (!m_db.open()) {
        const QSqlError error = m_db.lastError();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(kConnectionName);
        throwSqlError(QStringLiteral("cannot open the database file"), error,
                      info.absoluteFilePath());
    }

    m_path = info.absoluteFilePath();

    // Referential integrity is off by default in SQLite; every ON DELETE clause in the schema
    // depends on this pragma, so it is executed on the connection itself, once, right here.
    exec(QStringLiteral("PRAGMA foreign_keys = ON"));
}

void Database::close()
{
    if (m_db.isValid()) {
        if (m_db.isOpen()) {
            m_db.close();
        }
        m_db = QSqlDatabase(); // drop this object's reference *before* removing the connection
    }
    if (QSqlDatabase::contains(kConnectionName)) {
        QSqlDatabase::removeDatabase(kConnectionName);
    }
}

bool Database::isOpen() const
{
    return m_db.isValid() && m_db.isOpen();
}

QSqlDatabase& Database::handle()
{
    if (!isOpen()) {
        throw core::DatabaseException("the database is not open", m_path.toStdString());
    }
    return m_db;
}

QSqlQuery Database::exec(const QString& sql)
{
    QSqlDatabase& db = handle();
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        throwSqlError(QStringLiteral("statement failed"), query.lastError(), sql);
    }
    return query;
}

QSqlQuery Database::prepared(const QString& sql, const QVariantList& binds)
{
    QSqlDatabase& db = handle();
    QSqlQuery query(db);
    if (!query.prepare(sql)) {
        throwSqlError(QStringLiteral("cannot prepare statement"), query.lastError(), sql);
    }
    for (const QVariant& value : binds) {
        query.addBindValue(value);
    }
    if (!query.exec()) {
        throwSqlError(QStringLiteral("prepared statement failed"), query.lastError(), sql);
    }
    return query;
}

void Database::transaction(const std::function<void()>& body)
{
    if (!body) {
        throw core::DatabaseException("transaction body is empty", "Database::transaction");
    }

    QSqlDatabase& db = handle();

    /// Nesting depth of the currently running unit of work; single-threaded by contract (§9).
    static int s_depth = 0;

    if (s_depth > 0) {
        // Already inside a transaction: join it rather than issuing an illegal nested BEGIN.
        // A throw from here still unwinds into the outer catch below, which rolls everything back.
        DepthGuard guard(s_depth);
        body();
        return;
    }

    if (!db.transaction()) {
        throwSqlError(QStringLiteral("cannot begin transaction"), db.lastError(), m_path);
    }

    try {
        DepthGuard guard(s_depth);
        body();
    } catch (...) {
        /// @oop-concept Rethrow :: undo the half-finished work, then let the ORIGINAL exception
        /// continue to the caller untouched — the caller still sees the real cause, not a
        /// substitute "transaction failed" error invented here.
        db.rollback();
        throw;
    }

    if (!db.commit()) {
        const QSqlError error = db.lastError();
        db.rollback();
        throwSqlError(QStringLiteral("cannot commit transaction"), error, m_path);
    }
}

} // namespace aluchop::persistence

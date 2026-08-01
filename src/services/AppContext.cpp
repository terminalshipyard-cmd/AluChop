/**
 * @file AppContext.cpp
 * @brief The composition root: one constructor that builds the entire application graph.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * @par Why declaration order is the design
 * C++ initialises data members strictly in the order they are **declared**, regardless of the order
 * the initialiser list happens to be written in, and destroys them in exactly the reverse order.
 * `AppContext` leans on that guarantee instead of on a hand-rolled `init()` sequence:
 *
 *  1. `m_bootstrap` is declared first, so opening, migrating and seeding the SQLite database is
 *     finished before any repository member exists. Repositories talk to `Database::instance()`,
 *     so a repository constructed before the connection was open would be a latent crash. There is
 *     no way to get this wrong by accident — the compiler enforces it.
 *  2. The repositories follow, then the raw-file layer (`AuditTrail`, `BackupManager`).
 *  3. The services come last, in dependency order: the notification bus, then the audit service,
 *     then everything that borrows them. Every service holds plain references, so a service can
 *     only reference something already fully constructed above it.
 *
 * Destruction unwinds the same list backwards: services die first, then the audit file is flushed
 * and closed, then the database connection. Nothing is `new`ed, nothing is a global, no destructor
 * order has to be remembered by a human.
 *
 * The initialiser list below is therefore written in declaration order deliberately — matching it
 * is what keeps `-Wreorder` silent and, more importantly, keeps the written order honest about
 * what actually happens at run time.
 */

#include "aluchop/services/AppContext.hpp"

#include <QDir>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Logger.hpp"

namespace aluchop::services {

namespace {

/// @brief File name of the SQLite database inside the data directory.
const QString kDatabaseFile = QStringLiteral("aluchop.db");

/// @brief File name of the fixed-record binary audit trail inside the data directory.
const QString kAuditFile = QStringLiteral("audit.bin");

/// @brief Sub-directory of the data directory that holds timestamped `.db` snapshots.
const QString kBackupDirName = QStringLiteral("backups");

/// @brief Sub-directory of the data directory that holds the append-mode text log.
const QString kLogFileName = QStringLiteral("logs/aluchop.log");

/**
 * @brief Absolute path of @p leaf inside @p dataDir.
 *
 * Built with QDir rather than by concatenating strings so that a trailing separator on @p dataDir,
 * or a Windows-style separator, cannot produce a path with a doubled or missing slash.
 */
QString pathIn(const QString& dataDir, const QString& leaf) {
    return QDir(dataDir).filePath(leaf);
}

} // namespace

// -------------------------------------------------------------------------------------------
// DbBootstrap — the first member, and therefore the first thing that runs
// -------------------------------------------------------------------------------------------

/**
 * @brief Creates the data directory, opens the database and brings the schema up to date.
 *
 * Expressing start-up as a *member's constructor* rather than as a statement in AppContext's
 * constructor body is what makes the ordering guarantee real: a constructor body runs only after
 * every member has been constructed, so a `Database::instance().open(...)` written there would
 * happen far too late.
 */
/// @oop-concept Objects as Members :: start-up side effects modelled as a member so that the
/// language's own initialisation order enforces them, rather than a comment asking nicely
AppContext::DbBootstrap::DbBootstrap(const QString& dataDir, const QString& menuSeedJsonPath) {
    // SQLite creates the database file but never its parent directory.
    QDir dir(dataDir);
    if (!dir.exists() && !QDir().mkpath(dataDir)) {
        throw core::FileIOException("could not create the data directory", dataDir.toStdString());
    }

    // Point the append-mode text logger at the data directory too, so a packaged build writes its
    // log beside its database instead of beside the working directory it happened to start in.
    try {
        core::Logger::instance().setLogFile(pathIn(dataDir, kLogFileName));
    } catch (const core::FileIOException&) {
        // An unwritable log directory is not a reason to refuse to start the restaurant.
    }

    persistence::Database& db = persistence::Database::instance();
    db.open(pathIn(dataDir, kDatabaseFile));   // throws core::DatabaseException on failure

    // Versioned DDL plus, on a genuinely first run, the seed data: the menu from JSON, the admin
    // account, tables T1..T12, suppliers, ingredients, recipes and the two launch promo codes.
    persistence::SchemaMigrator migrator(db);
    migrator.migrate(menuSeedJsonPath);        // throws Database/Validation exceptions on failure
}

// -------------------------------------------------------------------------------------------
// AppContext
// -------------------------------------------------------------------------------------------

/// @oop-concept Pass by Reference :: every service below is handed its collaborators by reference
/// and owns none of them, which is what keeps the layer dependencies strictly one-directional
AppContext::AppContext(const QString& dataDir, const QString& menuSeedJsonPath)
    // (1) database open + migrated + seeded — MUST be first, see the file comment above
    : m_bootstrap(dataDir, menuSeedJsonPath),

      // (2) repositories. Each is a stateless handle over the connection opened in step 1, so
      //     their default constructors do no work beyond recording their table name.

      // (3) raw-file layer. AuditTrail opens `<dataDir>/audit.bin` and resumes its sequence
      //     numbering from the last record already in the file.
      m_auditTrail(pathIn(dataDir, kAuditFile)),
      m_backups(pathIn(dataDir, kDatabaseFile), pathIn(dataDir, kBackupDirName)),

      // (4) services, dependencies first. The bus has no parent: AppContext owns it by value, and
      //     giving a value member a QObject parent would hand a second owner the right to delete it.
      m_notify(nullptr),
      m_auditSvc(m_auditTrail, m_auditRepo),
      m_auth(m_userRepo, m_settingsRepo, m_auditSvc),
      m_menuSvc(m_menuRepo, m_auditSvc, m_notify),
      m_customerSvc(m_customerRepo, m_orderRepo, m_auditSvc, m_notify),
      m_employeeSvc(m_employeeRepo, m_customerRepo, m_auditSvc, m_notify),
      m_inventorySvc(m_ingredientRepo, m_supplierRepo, m_menuRepo, m_auditSvc, m_notify),
      m_orderSvc(m_orderRepo, m_menuRepo, m_tableRepo, m_inventorySvc, m_customerSvc,
                 m_auditSvc, m_notify),
      m_billingSvc(m_orderRepo, m_paymentRepo, m_promoRepo, m_customerSvc, m_employeeSvc,
                   m_auditSvc, m_notify),
      m_reservationSvc(m_reservationRepo, m_tableRepo, m_customerRepo, m_auditSvc, m_notify),
      m_reportSvc(m_paymentRepo, m_orderRepo, m_customerRepo, m_employeeRepo, m_ingredientRepo),
      m_settingsSvc(m_settingsRepo, m_backups, m_auditSvc),
      m_commands() {

    // The kitchen pass is not persisted as a queue — it is *derived* from the orders that are
    // still Pending. Rebuilding it here means closing the application mid-service and reopening it
    // puts every waiting ticket back in the right order instead of losing the floor's workload.
    m_orderSvc.rebuildQueue();

    // The first record of every run, written as a system action: nobody is logged in yet.
    // AppContext never authenticates anybody — deciding between the login window and a remembered
    // session belongs to main.cpp, which is the only place that knows what the user should see.
    m_auditSvc.setActiveUser(0);
    m_auditSvc.log(QStringLiteral("APP_START"), QStringLiteral("app"), core::Money(),
                   QStringLiteral("data dir %1").arg(dataDir));
}

} // namespace aluchop::services

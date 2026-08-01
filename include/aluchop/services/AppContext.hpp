#pragma once

/**
 * @file AppContext.hpp
 * @brief The composition root: one object that owns the whole application graph.
 *
 * Everything the application needs — the database, the raw audit file, every repository and every
 * service — is a **value member** of this class, wired together by reference in the constructor's
 * initialiser list. Nothing is allocated with `new`, nothing is a global, and destruction happens
 * in exactly the reverse order of construction, automatically.
 *
 * @warning Declaration order below **is** initialisation order. Do not reorder it: `m_bootstrap`
 * must open and migrate the database before the first repository is constructed, and each service
 * must be constructed after the services it borrows.
 */

#include <QString>

#include "aluchop/persistence/AuditRepository.hpp"
#include "aluchop/persistence/AuditTrail.hpp"
#include "aluchop/persistence/BackupManager.hpp"
#include "aluchop/persistence/CustomerRepository.hpp"
#include "aluchop/persistence/Database.hpp"
#include "aluchop/persistence/EmployeeRepository.hpp"
#include "aluchop/persistence/IngredientRepository.hpp"
#include "aluchop/persistence/MenuRepository.hpp"
#include "aluchop/persistence/OrderRepository.hpp"
#include "aluchop/persistence/PaymentRepository.hpp"
#include "aluchop/persistence/PromoRepository.hpp"
#include "aluchop/persistence/ReservationRepository.hpp"
#include "aluchop/persistence/SchemaMigrator.hpp"
#include "aluchop/persistence/SettingsRepository.hpp"
#include "aluchop/persistence/SupplierRepository.hpp"
#include "aluchop/persistence/TableRepository.hpp"
#include "aluchop/persistence/UserRepository.hpp"

#include "aluchop/services/AuditService.hpp"
#include "aluchop/services/AuthService.hpp"
#include "aluchop/services/BillingService.hpp"
#include "aluchop/services/Commands.hpp"
#include "aluchop/services/CustomerService.hpp"
#include "aluchop/services/EmployeeService.hpp"
#include "aluchop/services/InventoryService.hpp"
#include "aluchop/services/MenuService.hpp"
#include "aluchop/services/NotificationService.hpp"
#include "aluchop/services/OrderService.hpp"
#include "aluchop/services/ReportService.hpp"
#include "aluchop/services/ReservationService.hpp"
#include "aluchop/services/SettingsService.hpp"

namespace aluchop::services {

/**
 * @brief Owns and wires every repository and service in the application.
 *
 * /// @oop-concept Objects as Members :: the whole object graph is composed of value members —
 * /// no manual lifetime management anywhere, and the destruction order falls out of the language
 * /// @oop-concept Pass by Reference :: services receive their collaborators by reference and
 * /// never own them, which is what keeps the layer dependencies one-directional
 */
class AppContext {
public:
    /**
     * @brief Opens `<dataDir>/aluchop.db`, migrates and seeds it, opens `<dataDir>/audit.bin`,
     *        then constructs every repository and service.
     * @param dataDir writable directory for the database, the audit file and backups.
     * @param menuSeedJsonPath path to `assets/menu/menu_seed.json`, used on first run only.
     * @throws core::DatabaseException when the database cannot be opened or migrated.
     * @throws core::FileIOException when the audit file cannot be opened.
     * @throws core::ValidationException when the menu seed file is malformed.
     * @post the kitchen queue has been rebuilt from persisted Pending orders. No user is logged in —
     *       authentication is `main.cpp`'s decision, never the context's.
     */
    explicit AppContext(const QString& dataDir, const QString& menuSeedJsonPath);

    ~AppContext() = default;

    /// The context owns unique resources (a DB connection, an open file) — copying is meaningless.
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    /// @name Service accessors
    /// @brief Handed to the GUI by reference; the context outlives every window.
    /// @oop-concept Return by Reference :: one live service per concern, never a copy
    /// @{
    AuthService&         auth()          noexcept { return m_auth; }
    MenuService&         menu()          noexcept { return m_menuSvc; }
    OrderService&        orders()        noexcept { return m_orderSvc; }
    BillingService&      billing()       noexcept { return m_billingSvc; }
    CustomerService&     customers()     noexcept { return m_customerSvc; }
    EmployeeService&     employees()     noexcept { return m_employeeSvc; }
    InventoryService&    inventory()     noexcept { return m_inventorySvc; }
    ReservationService&  reservations()  noexcept { return m_reservationSvc; }
    ReportService&       reports()       noexcept { return m_reportSvc; }
    SettingsService&     settings()      noexcept { return m_settingsSvc; }
    AuditService&        audit()         noexcept { return m_auditSvc; }
    NotificationService& notifications() noexcept { return m_notify; }
    CommandStack&        commands()      noexcept { return m_commands; }
    /// @}

private:
    /**
     * @brief Startup side effects expressed as a member so they happen at the right moment.
     *
     * Declaring this as the first data member is what guarantees the database is open, migrated
     * and seeded **before** any repository below is constructed — an ordering guarantee the
     * language gives for free, and which a separate `init()` call would not.
     */
    struct DbBootstrap {
        DbBootstrap(const QString& dataDir, const QString& menuSeedJsonPath);
    };

    DbBootstrap m_bootstrap;   ///< MUST stay the first member, before all repositories

    // --- repositories (values) ------------------------------------------------------------
    persistence::UserRepository        m_userRepo;
    persistence::MenuRepository        m_menuRepo;
    persistence::CustomerRepository    m_customerRepo;
    persistence::EmployeeRepository    m_employeeRepo;
    persistence::OrderRepository       m_orderRepo;
    persistence::IngredientRepository  m_ingredientRepo;
    persistence::SupplierRepository    m_supplierRepo;
    persistence::TableRepository       m_tableRepo;
    persistence::ReservationRepository m_reservationRepo;
    persistence::PaymentRepository     m_paymentRepo;
    persistence::PromoRepository       m_promoRepo;
    persistence::SettingsRepository    m_settingsRepo;
    persistence::AuditRepository       m_auditRepo;

    // --- raw-file layer -------------------------------------------------------------------
    persistence::AuditTrail            m_auditTrail;   ///< `<dataDir>/audit.bin`
    persistence::BackupManager         m_backups;

    // --- services (dependencies first) -----------------------------------------------------
    NotificationService  m_notify;
    AuditService         m_auditSvc;
    AuthService          m_auth;
    MenuService          m_menuSvc;
    CustomerService      m_customerSvc;
    EmployeeService      m_employeeSvc;
    InventoryService     m_inventorySvc;
    OrderService         m_orderSvc;
    BillingService       m_billingSvc;
    ReservationService   m_reservationSvc;
    ReportService        m_reportSvc;
    SettingsService      m_settingsSvc;
    CommandStack         m_commands;
};

} // namespace aluchop::services

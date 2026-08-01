/**
 * @file SchemaMigrator.cpp
 * @brief Versioned DDL plus the once-only seeding of the AluChop database (ARCHITECTURE §6).
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Two responsibilities, deliberately kept in one place because they are the same event:
 *
 *  1. **Migration.** `settings['schema_version']` records the shape of the database (absent == 0).
 *     Migration `n -> n+1` is applied inside a single `Database::transaction`, so a failure leaves
 *     the file exactly as it was. Shipped DDL is immutable: a future change appends
 *     `applyMigration2()`, it never edits `applyMigration1()`.
 *
 *  2. **Seeding.** On a first run only, the 126-item menu, its 157 ingredients and 6 suppliers are
 *     read from `assets/menu/menu_seed.json`, together with the twelve tables, the staff roster,
 *     the `admin` login (salted SHA-256, never plaintext) and the two launch promo codes. Seeding
 *     is guarded by a row count, so a second launch adds nothing — the whole operation is
 *     idempotent by construction, not by luck.
 *
 * Prices arriving from the seed file are **already tax-inclusive** paisa integers and are stored
 * verbatim: no code path in this file scales, taxes or rounds a price.
 */

#include "aluchop/persistence/SchemaMigrator.hpp"

#include <array>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::persistence {
namespace {

/// @oop-concept Constants :: the settings key that carries the schema version, written once
const QString kSchemaVersionKey = QStringLiteral("schema_version");

/// UPSERT used for every settings write in this file (SQLite's portable form).
const QString kSettingsUpsert =
    QStringLiteral("INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)");

/**
 * @brief Migration 1 — the complete schema of docs/ARCHITECTURE.md §6.
 *
 * Seventeen tables and eleven indices, transcribed verbatim from the contract with `IF NOT EXISTS`
 * added so that re-running the migration on a partially created database converges instead of
 * failing. CHECK constraints spell out exactly the tokens the `models::Enums` conversions produce,
 * which makes an invalid enum a database error rather than silent bad data.
 *
 * /// @oop-concept Arrays :: the DDL is a plain array of statements, executed in order
 */
const std::array<const char*, 28> kMigration1 = {{
    // ---- tables ------------------------------------------------------------------------------
    "CREATE TABLE IF NOT EXISTS settings ("
    "  key   TEXT PRIMARY KEY,"
    "  value TEXT NOT NULL"
    ")",

    "CREATE TABLE IF NOT EXISTS suppliers ("
    "  id      INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name    TEXT NOT NULL,"
    "  phone   TEXT,"
    "  email   TEXT,"
    "  address TEXT"
    ")",

    "CREATE TABLE IF NOT EXISTS ingredients ("
    "  id                  INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name                TEXT NOT NULL UNIQUE,"
    "  unit                TEXT NOT NULL,"
    "  stock_qty           REAL NOT NULL DEFAULT 0 CHECK (stock_qty >= 0),"
    "  low_stock_threshold REAL NOT NULL DEFAULT 0 CHECK (low_stock_threshold >= 0),"
    "  expiry_date         TEXT,"
    "  unit_cost_paisa     INTEGER NOT NULL DEFAULT 0 CHECK (unit_cost_paisa >= 0),"
    "  supplier_id         INTEGER REFERENCES suppliers(id) ON DELETE SET NULL"
    ")",

    "CREATE TABLE IF NOT EXISTS menu_items ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name         TEXT NOT NULL,"
    "  category     TEXT NOT NULL,"
    "  price_paisa  INTEGER NOT NULL CHECK (price_paisa >= 0),"
    "  description  TEXT NOT NULL DEFAULT '',"
    "  image_path   TEXT NOT NULL DEFAULT '',"
    "  is_available INTEGER NOT NULL DEFAULT 1 CHECK (is_available IN (0,1)),"
    "  created_at   TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))"
    ")",

    "CREATE TABLE IF NOT EXISTS recipes ("
    "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  menu_item_id    INTEGER NOT NULL REFERENCES menu_items(id) ON DELETE CASCADE,"
    "  ingredient_id   INTEGER NOT NULL REFERENCES ingredients(id) ON DELETE CASCADE,"
    "  qty_per_serving REAL NOT NULL CHECK (qty_per_serving > 0),"
    "  UNIQUE (menu_item_id, ingredient_id)"
    ")",

    "CREATE TABLE IF NOT EXISTS employees ("
    "  id                 INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name               TEXT NOT NULL,"
    "  phone              TEXT,"
    "  email              TEXT,"
    "  position           TEXT NOT NULL CHECK (position IN ('WAITER','CHEF','MANAGER','ADMIN')),"
    "  salary_paisa       INTEGER NOT NULL DEFAULT 0 CHECK (salary_paisa >= 0),"
    "  shift              TEXT NOT NULL DEFAULT 'DAY',"
    "  hired_date         TEXT,"
    "  is_active          INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0,1)),"
    "  performance_rating INTEGER NOT NULL DEFAULT 3 CHECK (performance_rating BETWEEN 1 AND 5)"
    ")",

    "CREATE TABLE IF NOT EXISTS attendance ("
    "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  employee_id INTEGER NOT NULL REFERENCES employees(id) ON DELETE CASCADE,"
    "  work_date   TEXT NOT NULL,"
    "  check_in    TEXT,"
    "  check_out   TEXT,"
    "  status      TEXT NOT NULL DEFAULT 'PRESENT' CHECK (status IN ('PRESENT','ABSENT','LEAVE')),"
    "  UNIQUE (employee_id, work_date)"
    ")",

    "CREATE TABLE IF NOT EXISTS customers ("
    "  id             INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name           TEXT NOT NULL,"
    "  phone          TEXT UNIQUE,"
    "  email          TEXT,"
    "  loyalty_points INTEGER NOT NULL DEFAULT 0 CHECK (loyalty_points >= 0),"
    "  visits         INTEGER NOT NULL DEFAULT 0 CHECK (visits >= 0),"
    "  created_at     TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))"
    ")",

    "CREATE TABLE IF NOT EXISTS users ("
    "  id                   INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  username             TEXT NOT NULL UNIQUE,"
    "  pass_hash            TEXT NOT NULL,"
    "  salt                 TEXT NOT NULL,"
    "  role                 TEXT NOT NULL CHECK (role IN ('ADMIN','MANAGER','WAITER','CHEF')),"
    "  employee_id          INTEGER REFERENCES employees(id) ON DELETE SET NULL,"
    "  security_question    TEXT NOT NULL DEFAULT '',"
    "  security_answer_hash TEXT NOT NULL DEFAULT '',"
    "  remember_token       TEXT NOT NULL DEFAULT '',"
    "  created_at           TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))"
    ")",

    "CREATE TABLE IF NOT EXISTS tables ("
    "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name      TEXT NOT NULL UNIQUE,"
    "  capacity  INTEGER NOT NULL CHECK (capacity >= 1),"
    "  is_active INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0,1))"
    ")",

    "CREATE TABLE IF NOT EXISTS orders ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  order_number TEXT NOT NULL UNIQUE,"
    "  type         TEXT NOT NULL CHECK (type IN ('DINE_IN','TAKEAWAY','DELIVERY')),"
    "  status       TEXT NOT NULL DEFAULT 'OPEN'"
    "               CHECK (status IN ('OPEN','PENDING','PREPARING','READY','SERVED','PAID','CANCELLED')),"
    "  table_id     INTEGER REFERENCES tables(id) ON DELETE SET NULL,"
    "  customer_id  INTEGER REFERENCES customers(id) ON DELETE SET NULL,"
    "  waiter_id    INTEGER REFERENCES employees(id) ON DELETE SET NULL,"
    "  note         TEXT NOT NULL DEFAULT '',"
    "  merged_into  INTEGER REFERENCES orders(id) ON DELETE SET NULL,"
    "  created_at   TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))"
    ")",

    "CREATE TABLE IF NOT EXISTS order_items ("
    "  id               INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  order_id         INTEGER NOT NULL REFERENCES orders(id) ON DELETE CASCADE,"
    "  menu_item_id     INTEGER REFERENCES menu_items(id) ON DELETE SET NULL,"
    "  name_snapshot    TEXT NOT NULL,"
    "  unit_price_paisa INTEGER NOT NULL CHECK (unit_price_paisa >= 0),"
    "  qty              INTEGER NOT NULL CHECK (qty >= 1),"
    "  line_note        TEXT NOT NULL DEFAULT ''"
    ")",

    "CREATE TABLE IF NOT EXISTS reservations ("
    "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  customer_id     INTEGER REFERENCES customers(id) ON DELETE SET NULL,"
    "  customer_name   TEXT NOT NULL,"
    "  phone           TEXT,"
    "  table_id        INTEGER NOT NULL REFERENCES tables(id) ON DELETE CASCADE,"
    "  starts_at       TEXT NOT NULL,"
    "  duration_min    INTEGER NOT NULL DEFAULT 90 CHECK (duration_min >= 15),"
    "  guests          INTEGER NOT NULL CHECK (guests >= 1),"
    "  special_request TEXT NOT NULL DEFAULT '',"
    "  status          TEXT NOT NULL DEFAULT 'BOOKED'"
    "                  CHECK (status IN ('BOOKED','SEATED','COMPLETED','CANCELLED','NO_SHOW'))"
    ")",

    "CREATE TABLE IF NOT EXISTS promos ("
    "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  code            TEXT NOT NULL UNIQUE,"
    "  kind            TEXT NOT NULL CHECK (kind IN ('PERCENT','FLAT')),"
    "  percent         INTEGER NOT NULL DEFAULT 0 CHECK (percent BETWEEN 0 AND 100),"
    "  flat_paisa      INTEGER NOT NULL DEFAULT 0 CHECK (flat_paisa >= 0),"
    "  min_order_paisa INTEGER NOT NULL DEFAULT 0 CHECK (min_order_paisa >= 0),"
    "  valid_from      TEXT,"
    "  valid_to        TEXT,"
    "  is_active       INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0,1))"
    ")",

    "CREATE TABLE IF NOT EXISTS payments ("
    "  id                   INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  order_id             INTEGER NOT NULL REFERENCES orders(id) ON DELETE CASCADE,"
    "  method               TEXT NOT NULL CHECK (method IN ('CASH','CARD','WALLET')),"
    "  subtotal_paisa       INTEGER NOT NULL CHECK (subtotal_paisa >= 0),"
    "  discount_paisa       INTEGER NOT NULL DEFAULT 0 CHECK (discount_paisa >= 0),"
    "  service_charge_paisa INTEGER NOT NULL DEFAULT 0 CHECK (service_charge_paisa >= 0),"
    "  total_paisa          INTEGER NOT NULL CHECK (total_paisa >= 0),"
    "  tendered_paisa       INTEGER NOT NULL CHECK (tendered_paisa >= 0),"
    "  change_paisa         INTEGER NOT NULL DEFAULT 0 CHECK (change_paisa >= 0),"
    "  promo_id             INTEGER REFERENCES promos(id) ON DELETE SET NULL,"
    "  received_by          INTEGER REFERENCES users(id) ON DELETE SET NULL,"
    "  paid_at              TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))"
    ")",

    "CREATE TABLE IF NOT EXISTS inventory_transactions ("
    "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  ingredient_id   INTEGER NOT NULL REFERENCES ingredients(id) ON DELETE CASCADE,"
    "  delta_qty       REAL NOT NULL,"
    "  reason          TEXT NOT NULL CHECK (reason IN ('RESTOCK','USAGE','WASTE','ADJUST')),"
    "  ref_order_id    INTEGER REFERENCES orders(id) ON DELETE SET NULL,"
    "  unit_cost_paisa INTEGER NOT NULL DEFAULT 0,"
    "  note            TEXT NOT NULL DEFAULT '',"
    "  created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))"
    ")",

    "CREATE TABLE IF NOT EXISTS audit_log ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  seq          INTEGER NOT NULL,"
    "  ts_utc_ms    INTEGER NOT NULL,"
    "  user_id      INTEGER NOT NULL DEFAULT 0,"
    "  action       TEXT NOT NULL,"
    "  entity       TEXT NOT NULL,"
    "  amount_paisa INTEGER NOT NULL DEFAULT 0,"
    "  details      TEXT NOT NULL DEFAULT ''"
    ")",

    // ---- indices -----------------------------------------------------------------------------
    "CREATE INDEX IF NOT EXISTS idx_orders_created     ON orders(created_at)",
    "CREATE INDEX IF NOT EXISTS idx_orders_status      ON orders(status)",
    "CREATE INDEX IF NOT EXISTS idx_order_items_order  ON order_items(order_id)",
    "CREATE INDEX IF NOT EXISTS idx_reservations_start ON reservations(starts_at)",
    "CREATE INDEX IF NOT EXISTS idx_reservations_table ON reservations(table_id, starts_at)",
    "CREATE INDEX IF NOT EXISTS idx_invtx_ingredient   ON inventory_transactions(ingredient_id, created_at)",
    "CREATE INDEX IF NOT EXISTS idx_payments_paid_at   ON payments(paid_at)",
    "CREATE INDEX IF NOT EXISTS idx_audit_ts           ON audit_log(ts_utc_ms)",
    "CREATE INDEX IF NOT EXISTS idx_menu_category      ON menu_items(category, name)",
    "CREATE INDEX IF NOT EXISTS idx_customers_phone    ON customers(phone)",
    "CREATE INDEX IF NOT EXISTS idx_attendance_emp     ON attendance(employee_id, work_date)",
}};

/// @brief 16 cryptographically random bytes as lowercase hex — the per-user password salt.
QString randomSaltHex()
{
    QByteArray raw(16, '\0');
    for (int i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return QString::fromLatin1(raw.toHex());
}

/**
 * @brief `SHA-256(salt + secret)` as lowercase hex.
 *
 * Byte-for-byte the scheme documented for `services::AuthService::hashPassword`, repeated here
 * because the persistence layer may not depend on services — the seeded `admin` credential has to
 * verify against exactly the same computation at login time.
 */
QString sha256Hex(const QString& salt, const QString& secret)
{
    const QByteArray digest =
        QCryptographicHash::hash((salt + secret).toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

/// @brief Runs an INSERT and returns the AUTOINCREMENT id SQLite assigned to the new row.
int insertRow(Database& db, const QString& sql, const QVariantList& binds)
{
    QSqlQuery inserted = db.prepared(sql, binds);
    const QVariant id = inserted.lastInsertId();
    if (id.isValid()) {
        return id.toInt();
    }
    QSqlQuery fallback = db.exec(QStringLiteral("SELECT last_insert_rowid()"));
    return fallback.next() ? fallback.value(0).toInt() : 0;
}

/// @brief Reads a JSON object member as text, tolerating an absent key.
QString jsonText(const QJsonObject& obj, const char* key)
{
    return obj.value(QLatin1String(key)).toString();
}

/// @brief Reads a JSON object member as a 64-bit integer (paisa counts, day offsets, ids).
qint64 jsonInt(const QJsonObject& obj, const char* key, qint64 fallback = 0)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    if (!value.isDouble()) {
        return fallback;
    }
    // toInteger(), not toDouble(): paisa amounts are read straight into a 64-bit integer so no
    // money value ever passes through a floating-point variable, not even in transit.
    return value.toInteger(fallback);
}

/// @brief Reads a JSON object member as a physical quantity (kg / l / pcs — never money).
double jsonReal(const QJsonObject& obj, const char* key, double fallback = 0.0)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    return value.isDouble() ? value.toDouble() : fallback;
}

/**
 * @brief Parses and validates the seed document.
 * @throws core::ValidationException when the file is missing, unreadable, not JSON, or does not
 *         carry the three arrays the seeding step needs.
 */
QJsonObject loadSeedDocument(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();

    QFile file(absolute);
    if (!file.exists()) {
        throw core::ValidationException("menu seed file not found", absolute.toStdString());
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw core::ValidationException("menu seed file cannot be read", absolute.toStdString());
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        throw core::ValidationException(
            "menu seed file is not valid JSON: " + parseError.errorString().toStdString(),
            absolute.toStdString(), parseError.offset);
    }
    if (!doc.isObject()) {
        throw core::ValidationException("menu seed file must contain a JSON object",
                                        absolute.toStdString());
    }

    const QJsonObject root = doc.object();
    for (const char* required : {"suppliers", "ingredients", "items"}) {
        if (!root.value(QLatin1String(required)).isArray()) {
            throw core::ValidationException(
                std::string("menu seed file is missing the '") + required + "' array",
                absolute.toStdString());
        }
    }
    if (root.value(QStringLiteral("items")).toArray().isEmpty()) {
        throw core::ValidationException("menu seed file contains no menu items",
                                        absolute.toStdString());
    }
    return root;
}

} // namespace

SchemaMigrator::SchemaMigrator(Database& db) : m_db(db) {}

int SchemaMigrator::currentVersion() const
{
    try {
        QSqlQuery query = m_db.prepared(
            QStringLiteral("SELECT value FROM settings WHERE key = ?"), {kSchemaVersionKey});
        if (query.next()) {
            bool ok = false;
            const int version = query.value(0).toInt(&ok);
            return ok ? version : 0;
        }
    } catch (const core::DatabaseException&) {
        // The `settings` table does not exist yet: by definition that is schema version 0.
        return 0;
    }
    return 0;
}

void SchemaMigrator::migrate(const QString& menuSeedJsonPath)
{
    const int from = currentVersion();
    if (from >= kLatestVersion) {
        return; // already current — seeding is not repeated, which is what makes launch 2 clean
    }

    // One transaction covers DDL, seeding and the version stamp: either the database ends up fully
    // built and populated, or it stays untouched. There is no half-migrated state to recover from.
    m_db.transaction([this, from, &menuSeedJsonPath]() {
        if (from < 1) {
            applyMigration1();
            seedIfEmpty(menuSeedJsonPath);
        }
        m_db.prepared(kSettingsUpsert,
                      {kSchemaVersionKey, QString::number(kLatestVersion)});
    });
}

void SchemaMigrator::applyMigration1()
{
    // Executed by migrate() inside Database::transaction — this function must never open its own.
    for (const char* statement : kMigration1) {
        m_db.exec(QString::fromLatin1(statement));
    }
}

void SchemaMigrator::seedIfEmpty(const QString& menuSeedJsonPath)
{
    QSqlQuery existing = m_db.exec(QStringLiteral("SELECT COUNT(*) FROM menu_items"));
    if (existing.next() && existing.value(0).toInt() > 0) {
        return; // the guard that makes seeding idempotent across launches
    }

    const QJsonObject root = loadSeedDocument(menuSeedJsonPath);

    // ---- 1. suppliers ------------------------------------------------------------------------
    /// @oop-concept STL / Qt containers :: name -> primary key maps turn the seed file's
    /// human-readable cross-references into real foreign keys
    QHash<QString, int> supplierIds;
    const QJsonArray suppliers = root.value(QStringLiteral("suppliers")).toArray();
    for (const QJsonValue& value : suppliers) {
        const QJsonObject obj = value.toObject();
        const QString name = jsonText(obj, "name");
        if (name.isEmpty()) {
            throw core::ValidationException("menu seed: a supplier has no name", "suppliers");
        }
        const int id = insertRow(
            m_db,
            QStringLiteral("INSERT INTO suppliers (name, phone, email, address) VALUES (?, ?, ?, ?)"),
            {name, jsonText(obj, "phone"), jsonText(obj, "email"), jsonText(obj, "address")});
        supplierIds.insert(name, id);
    }

    // ---- 2. ingredients ----------------------------------------------------------------------
    QHash<QString, int> ingredientIds;
    const QDate today = QDate::currentDate();
    const QJsonArray ingredients = root.value(QStringLiteral("ingredients")).toArray();
    for (const QJsonValue& value : ingredients) {
        const QJsonObject obj = value.toObject();
        const QString name = jsonText(obj, "name");
        if (name.isEmpty()) {
            throw core::ValidationException("menu seed: an ingredient has no name", "ingredients");
        }

        const QString supplierName = jsonText(obj, "supplier_name");
        const QVariant supplierId = supplierIds.contains(supplierName)
                                        ? QVariant(supplierIds.value(supplierName))
                                        : QVariant(QMetaType(QMetaType::Int));

        const qint64 expiryDays = jsonInt(obj, "expiry_days", 0);
        const QVariant expiry = expiryDays > 0
                                    ? QVariant(today.addDays(expiryDays).toString(Qt::ISODate))
                                    : QVariant(QMetaType(QMetaType::QString));

        const int id = insertRow(
            m_db,
            QStringLiteral("INSERT INTO ingredients (name, unit, stock_qty, low_stock_threshold,"
                           " expiry_date, unit_cost_paisa, supplier_id) VALUES (?, ?, ?, ?, ?, ?, ?)"),
            {name, jsonText(obj, "unit"), jsonReal(obj, "stock_quantity"),
             jsonReal(obj, "reorder_level"), expiry, jsonInt(obj, "cost_per_unit_paisa"),
             supplierId});
        ingredientIds.insert(name, id);
    }

    // ---- 3. menu items + recipes ---------------------------------------------------------------
    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    for (const QJsonValue& value : items) {
        const QJsonObject obj = value.toObject();
        const QString name = jsonText(obj, "name");
        const QString category = jsonText(obj, "category");
        if (name.isEmpty() || category.isEmpty()) {
            throw core::ValidationException("menu seed: an item is missing its name or category",
                                            "items");
        }

        // The price is copied through untouched: seed prices are already tax-inclusive paisa.
        const bool available = obj.contains(QStringLiteral("is_available"))
                                   ? obj.value(QStringLiteral("is_available")).toBool(true)
                                   : obj.value(QStringLiteral("available")).toBool(true);
        const QString image = obj.contains(QStringLiteral("image_path"))
                                  ? jsonText(obj, "image_path")
                                  : jsonText(obj, "image");

        const int itemId = insertRow(
            m_db,
            QStringLiteral("INSERT INTO menu_items (name, category, price_paisa, description,"
                           " image_path, is_available) VALUES (?, ?, ?, ?, ?, ?)"),
            {name, category, jsonInt(obj, "price_paisa"), jsonText(obj, "description"), image,
             available ? 1 : 0});

        const QJsonArray recipe = obj.value(QStringLiteral("recipe")).toArray();
        for (const QJsonValue& lineValue : recipe) {
            const QJsonObject line = lineValue.toObject();
            const QString ingredientName = jsonText(line, "ingredient_name");
            const double qty = jsonReal(line, "quantity");
            if (qty <= 0.0) {
                continue; // a zero-quantity line would violate the CHECK and means nothing anyway
            }
            if (!ingredientIds.contains(ingredientName)) {
                throw core::ValidationException(
                    "menu seed: recipe for '" + name.toStdString() + "' refers to unknown ingredient",
                    ingredientName.toStdString());
            }
            m_db.prepared(
                QStringLiteral("INSERT OR IGNORE INTO recipes (menu_item_id, ingredient_id,"
                               " qty_per_serving) VALUES (?, ?, ?)"),
                {itemId, ingredientIds.value(ingredientName), qty});
        }
    }

    // ---- 4. dining tables T1..T12 --------------------------------------------------------------
    /// @oop-concept Arrays :: the seating plan is a fixed array of capacities, index-aligned to T1..T12
    const std::array<int, 12> kCapacities = {{2, 2, 2, 4, 4, 4, 4, 6, 6, 6, 8, 8}};
    for (std::size_t i = 0; i < kCapacities.size(); ++i) {
        m_db.prepared(QStringLiteral("INSERT INTO tables (name, capacity, is_active) VALUES (?, ?, 1)"),
                      {QStringLiteral("T%1").arg(i + 1), kCapacities[i]});
    }

    // ---- 5. staff roster ------------------------------------------------------------------------
    struct SeedEmployee {
        const char* name;
        const char* phone;
        const char* email;
        const char* position;
        qint64 salaryPaisa;   ///< integer paisa — payroll is money, so never a double
        const char* shift;
        int rating;
    };

    const std::array<SeedEmployee, 7> kStaff = {{
        {"Shashank Bhattarai", "+977-9801000001", "shashankbhattarai006@gmail.com", "ADMIN",
         12000000, "DAY", 5},
        {"Anjali Gurung", "+977-9801000002", "anjali.gurung@aluchop.com.np", "MANAGER",
         8500000, "DAY", 5},
        {"Bikash Tamang", "+977-9801000003", "bikash.tamang@aluchop.com.np", "CHEF",
         5500000, "NIGHT", 4},
        {"Sita Karki", "+977-9801000004", "sita.karki@aluchop.com.np", "CHEF", 4800000, "DAY", 4},
        {"Prakash Rai", "+977-9801000005", "prakash.rai@aluchop.com.np", "WAITER", 2800000, "DAY", 4},
        {"Nisha Thapa", "+977-9801000006", "nisha.thapa@aluchop.com.np", "WAITER", 2800000, "NIGHT", 3},
        {"Deepak Shrestha", "+977-9801000007", "deepak.shrestha@aluchop.com.np", "WAITER",
         2600000, "DAY", 3},
    }};

    int adminEmployeeId = 0;
    for (std::size_t i = 0; i < kStaff.size(); ++i) {
        const SeedEmployee& staff = kStaff[i];
        const int id = insertRow(
            m_db,
            QStringLiteral("INSERT INTO employees (name, phone, email, position, salary_paisa,"
                           " shift, hired_date, is_active, performance_rating)"
                           " VALUES (?, ?, ?, ?, ?, ?, ?, 1, ?)"),
            {QString::fromLatin1(staff.name), QString::fromLatin1(staff.phone),
             QString::fromLatin1(staff.email), QString::fromLatin1(staff.position),
             staff.salaryPaisa, QString::fromLatin1(staff.shift),
             today.addDays(-365 - static_cast<int>(i) * 45).toString(Qt::ISODate), staff.rating});
        if (adminEmployeeId == 0) {
            adminEmployeeId = id; // the first row is the owner/administrator
        }
    }

    // ---- 6. the admin login --------------------------------------------------------------------
    // Passwords are never stored: only SHA-256(salt + password) in hex, with a fresh random salt.
    const QString adminSalt = randomSaltHex();
    m_db.prepared(
        QStringLiteral("INSERT INTO users (username, pass_hash, salt, role, employee_id,"
                       " security_question, security_answer_hash, remember_token)"
                       " VALUES (?, ?, ?, 'ADMIN', ?, ?, ?, '')"),
        {QStringLiteral("admin"), sha256Hex(adminSalt, QStringLiteral("admin123")), adminSalt,
         adminEmployeeId, QStringLiteral("What is your roll number?"),
         sha256Hex(adminSalt, QStringLiteral("ACE082BCT078"))});

    // ---- 7. a starter customer book --------------------------------------------------------------
    struct SeedCustomer {
        const char* name;
        const char* phone;
        const char* email;
        int points;
        int visits;
    };

    const std::array<SeedCustomer, 6> kCustomers = {{
        {"Rojina Maharjan", "+977-9841000011", "rojina.maharjan@example.com", 120, 8},
        {"Sujan Adhikari", "+977-9841000012", "sujan.adhikari@example.com", 45, 3},
        {"Pratima Joshi", "+977-9841000013", "pratima.joshi@example.com", 260, 14},
        {"Kiran Lama", "+977-9841000014", "kiran.lama@example.com", 15, 1},
        {"Manish Basnet", "+977-9841000015", "manish.basnet@example.com", 80, 6},
        {"Aakriti Sharma", "+977-9841000016", "aakriti.sharma@example.com", 190, 11},
    }};

    for (const SeedCustomer& customer : kCustomers) {
        m_db.prepared(
            QStringLiteral("INSERT INTO customers (name, phone, email, loyalty_points, visits)"
                           " VALUES (?, ?, ?, ?, ?)"),
            {QString::fromLatin1(customer.name), QString::fromLatin1(customer.phone),
             QString::fromLatin1(customer.email), customer.points, customer.visits});
    }

    // ---- 8. launch promo codes -------------------------------------------------------------------
    m_db.prepared(
        QStringLiteral("INSERT INTO promos (code, kind, percent, flat_paisa, min_order_paisa,"
                       " valid_from, valid_to, is_active) VALUES (?, 'PERCENT', ?, 0, ?, NULL, NULL, 1)"),
        {QStringLiteral("WELCOME10"), 10, 0});
    m_db.prepared(
        QStringLiteral("INSERT INTO promos (code, kind, percent, flat_paisa, min_order_paisa,"
                       " valid_from, valid_to, is_active) VALUES (?, 'FLAT', 0, ?, ?, NULL, NULL, 1)"),
        {QStringLiteral("FLAT100"), 10000, 100000});

    // ---- 9. default preferences -------------------------------------------------------------------
    const QJsonObject meta = root.value(QStringLiteral("_meta")).toObject();
    const QString restaurantName = meta.value(QStringLiteral("restaurant")).toString(
        QStringLiteral("AluChop - Multi-Cuisine Restaurant & Bar"));
    const QString restaurantAddress = meta.value(QStringLiteral("location")).toString(
        QStringLiteral("Jhamsikhel, Lalitpur, Nepal"));

    const std::array<std::pair<QString, QString>, 5> kDefaults = {{
        {QStringLiteral("restaurant.name"), restaurantName},
        {QStringLiteral("restaurant.address"), restaurantAddress},
        {QStringLiteral("restaurant.phone"), QStringLiteral("+977-1-5455101")},
        {QStringLiteral("theme.mode"), QStringLiteral("light")},
        {QStringLiteral("billing.service_charge_pct"), QStringLiteral("10")},
    }};
    for (const auto& setting : kDefaults) {
        m_db.prepared(kSettingsUpsert, {setting.first, setting.second});
    }
}

} // namespace aluchop::persistence

/**
 * @file IngredientRepository.cpp
 * @brief The `ingredients` table plus the `inventory_transactions` ledger it owns.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Quantities here are `double` because they are physical measurements (0.75 kg of chicken); the only
 * money in this file is `unit_cost_paisa`, which travels as `core::Money` and integer paisa.
 *
 * Stock is never written blind. @ref adjustStock is the single door: it moves the running quantity
 * and appends the matching ledger row inside one transaction, so the ledger can always be replayed
 * to explain the number on the shelf.
 */

#include "aluchop/persistence/IngredientRepository.hpp"

#include <QDate>
#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {
namespace {

constexpr auto kTsFormat = "yyyy-MM-ddTHH:mm:ss";
constexpr auto kDateFormat = "yyyy-MM-dd";

QDateTime tsFromDb(const QString& s) {
    QDateTime dt = QDateTime::fromString(s, QString::fromLatin1(kTsFormat));
    if (!dt.isValid()) dt = QDateTime::fromString(s, Qt::ISODate);
    if (dt.isValid()) dt.setTimeZone(QTimeZone::UTC);
    return dt;
}

/// "Does not expire" is a genuine NULL, so the expiry queries can simply ignore those rows.
QVariant dateOrNull(QDate d) {
    return d.isValid() ? QVariant(d.toString(QString::fromLatin1(kDateFormat))) : QVariant();
}

QDate dateFromDb(const QVariant& v) {
    if (v.isNull()) return QDate();
    return QDate::fromString(v.toString(), QString::fromLatin1(kDateFormat));
}

QVariant fkOrNull(int id) { return id > 0 ? QVariant(id) : QVariant(); }

} // namespace

IngredientRepository::IngredientRepository() : Repository(QStringLiteral("ingredients")) {}

int IngredientRepository::insert(const models::Ingredient& i) {
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("INSERT INTO ingredients "
                       "(name, unit, stock_qty, low_stock_threshold, expiry_date, "
                       " unit_cost_paisa, supplier_id) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?)"),
        { i.name(), i.unit(), i.stockQty(), i.lowThreshold(), dateOrNull(i.expiryDate()),
          static_cast<qlonglong>(i.unitCost().paisa()), fkOrNull(i.supplierId()) });
    return q.lastInsertId().toInt();
}

void IngredientRepository::update(const models::Ingredient& i) {
    Database::instance().prepared(
        QStringLiteral("UPDATE ingredients SET name = ?, unit = ?, stock_qty = ?, "
                       "low_stock_threshold = ?, expiry_date = ?, unit_cost_paisa = ?, "
                       "supplier_id = ? WHERE id = ?"),
        { i.name(), i.unit(), i.stockQty(), i.lowThreshold(), dateOrNull(i.expiryDate()),
          static_cast<qlonglong>(i.unitCost().paisa()), fkOrNull(i.supplierId()), i.id() });
}

void IngredientRepository::adjustStock(int ingredientId, double deltaQty, const QString& reason,
                                       int refOrderId, core::Money unitCost, const QString& note) {
    Database& db = Database::instance();
    // Movement and journal entry are one indivisible fact. If the UPDATE trips the schema's
    // CHECK (stock_qty >= 0) — an over-deduction — Database::transaction rolls the whole thing back
    // and rethrows, so no orphan ledger row can survive to explain a change that never happened.
    db.transaction([&] {
        QSqlQuery upd = db.prepared(
            QStringLiteral("UPDATE ingredients SET stock_qty = stock_qty + ? WHERE id = ?"),
            { deltaQty, ingredientId });
        if (upd.numRowsAffected() == 0) {
            throw core::DatabaseException(
                "stock adjustment refers to an ingredient that does not exist",
                QStringLiteral("ingredients.id = %1").arg(ingredientId).toStdString());
        }
        db.prepared(
            QStringLiteral("INSERT INTO inventory_transactions "
                           "(ingredient_id, delta_qty, reason, ref_order_id, unit_cost_paisa, note) "
                           "VALUES (?, ?, ?, ?, ?, ?)"),
            { ingredientId, deltaQty, reason, fkOrNull(refOrderId),
              static_cast<qlonglong>(unitCost.paisa()), note });
    });
}

std::vector<models::Ingredient> IngredientRepository::lowStock() const {
    // Mirrors models::Ingredient::isLow() exactly (`<=`, not `<`), so the alert list and the row
    // badge can never disagree about which items need reordering.
    std::vector<models::Ingredient> out;
    QSqlQuery q = Database::instance().exec(
        QStringLiteral("SELECT * FROM ingredients WHERE stock_qty <= low_stock_threshold ORDER BY name"));
    while (q.next()) out.push_back(fromRecord(q.record()));
    return out;
}

std::vector<models::Ingredient> IngredientRepository::expiringWithin(int days) const {
    // Already-expired goods are included: they need attention more urgently than the ones merely
    // approaching their date, and hiding them would be the wrong kind of tidy.
    std::vector<models::Ingredient> out;
    const QDate horizon = QDate::currentDate().addDays(days);
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM ingredients WHERE expiry_date IS NOT NULL AND expiry_date <= ? "
                       "ORDER BY expiry_date, name"),
        { horizon.toString(QString::fromLatin1(kDateFormat)) });
    while (q.next()) out.push_back(fromRecord(q.record()));
    return out;
}

std::vector<std::tuple<QDateTime, double, QString, QString>>
IngredientRepository::history(int ingredientId, int limit) const {
    std::vector<std::tuple<QDateTime, double, QString, QString>> out;
    if (limit <= 0) return out;
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT created_at, delta_qty, reason, note FROM inventory_transactions "
                       "WHERE ingredient_id = ? ORDER BY created_at DESC, id DESC LIMIT ?"),
        { ingredientId, limit });
    while (q.next()) {
        out.emplace_back(tsFromDb(q.value(0).toString()),
                         q.value(1).toDouble(),
                         q.value(2).toString(),
                         q.value(3).toString());
    }
    return out;
}

models::Ingredient IngredientRepository::fromRecord(const QSqlRecord& rec) const {
    models::Ingredient i(rec.value(QStringLiteral("id")).toInt(),
                         rec.value(QStringLiteral("name")).toString(),
                         rec.value(QStringLiteral("unit")).toString(),
                         rec.value(QStringLiteral("stock_qty")).toDouble(),
                         rec.value(QStringLiteral("low_stock_threshold")).toDouble());
    i.setExpiryDate(dateFromDb(rec.value(QStringLiteral("expiry_date"))));
    i.setUnitCost(core::Money(rec.value(QStringLiteral("unit_cost_paisa")).toLongLong()));
    i.setSupplierId(rec.value(QStringLiteral("supplier_id")).toInt());
    return i;
}

QString IngredientRepository::orderByClause() const { return QStringLiteral("name"); }

} // namespace aluchop::persistence

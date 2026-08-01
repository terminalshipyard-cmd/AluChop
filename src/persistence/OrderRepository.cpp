/**
 * @file OrderRepository.cpp
 * @brief The `orders` header table plus its `order_items` lines.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * An order is always written as a unit. Header and lines go in one transaction, so a half-saved
 * order — a bill with a total but no dishes, or dishes attached to nothing — cannot exist even for
 * an instant. Line rows carry a *snapshot* of the dish name and unit price, which is why re-pricing
 * the menu at 8pm never rewrites the bill a guest signed at 7pm.
 *
 * Money moves as INTEGER paisa the whole way: `unit_price_paisa` ↔ `core::Money`, never through a
 * floating-point type, and never with tax added on top — menu prices are already tax-inclusive.
 */

#include "aluchop/persistence/OrderRepository.hpp"

#include <QDate>
#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>

#include "aluchop/models/OrderItem.hpp"
#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {
namespace {

constexpr auto kTsFormat = "yyyy-MM-ddTHH:mm:ss";

QString tsToDb(const QDateTime& dt) {
    return dt.toUTC().toString(QString::fromLatin1(kTsFormat));
}

QDateTime tsFromDb(const QString& s) {
    QDateTime dt = QDateTime::fromString(s, QString::fromLatin1(kTsFormat));
    if (!dt.isValid()) dt = QDateTime::fromString(s, Qt::ISODate);
    if (dt.isValid()) dt.setTimeZone(QTimeZone::UTC);
    return dt;
}

/// Optional foreign keys (table, customer, waiter, source menu row) are NULL when unset — binding 0
/// would be rejected by `PRAGMA foreign_keys = ON`, because no row anywhere has id 0.
QVariant fkOrNull(int id) { return id > 0 ? QVariant(id) : QVariant(); }

/**
 * @brief Drives a freshly built Order to the status stored in its row.
 *
 * `Order::setStatus()` enforces the kitchen pipeline — that is exactly what stops any code path
 * marking an order Paid straight from Open — and a hydrated Order necessarily starts at Open.
 * Hydration therefore *walks* the legal chain instead of trying to jump, which keeps the model's
 * invariant honest rather than working around it with a back door setter.
 */
void hydrateStatus(models::Order& o, models::OrderStatus target) {
    using S = models::OrderStatus;
    if (target == o.status()) return;
    if (target == S::Cancelled) {          // reachable directly from Open and from Pending
        o.setStatus(S::Cancelled);
        return;
    }

    constexpr int kChainLength = 6;
    static const S kChain[kChainLength] = { S::Open,   S::Pending, S::Preparing,
                                            S::Ready,  S::Served,  S::Paid };

    int current = 0;
    for (int i = 0; i < kChainLength; ++i)
        if (kChain[i] == o.status()) current = i;

    for (int i = current + 1; i < kChainLength; ++i) {
        o.setStatus(kChain[i]);
        if (kChain[i] == target) return;
    }
}

/// Writes every line of @p o. The caller has already opened a transaction.
void writeItems(Database& db, const models::Order& o) {
    for (const models::OrderItem& item : o.items()) {
        db.prepared(
            QStringLiteral("INSERT INTO order_items "
                           "(order_id, menu_item_id, name_snapshot, unit_price_paisa, qty, line_note) "
                           "VALUES (?, ?, ?, ?, ?, ?)"),
            { o.id(), fkOrNull(item.menuItemId()), item.name(),
              static_cast<qlonglong>(item.unitPrice().paisa()), item.qty(), item.note() });
    }
}

/// Attaches the persisted lines to an already-hydrated header.
void readItems(models::Order& o) {
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT menu_item_id, name_snapshot, unit_price_paisa, qty, line_note "
                       "FROM order_items WHERE order_id = ? ORDER BY id"),
        { o.id() });
    while (q.next()) {
        models::OrderItem item(q.value(0).toInt(),
                               q.value(1).toString(),
                               core::Money(q.value(2).toLongLong()),
                               q.value(3).toInt());
        item.setNote(q.value(4).toString());
        o.addItem(item);
    }
}

/**
 * @brief Allocates the next human-facing number for a day, `ORD-YYYYMMDD-NNN`.
 *
 * The suffix is derived from the highest number already issued for that day rather than from a row
 * count, so cancelling an order never causes the next one to reuse its number.
 */
QString nextOrderNumber(QDate utcDay) {
    const QString prefix =
        QStringLiteral("ORD-%1-").arg(utcDay.toString(QStringLiteral("yyyyMMdd")));

    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT order_number FROM orders WHERE order_number LIKE ? "
                       "ORDER BY order_number DESC LIMIT 1"),
        { prefix + QStringLiteral("%") });

    int next = 1;
    if (q.next()) {
        bool ok = false;
        const int used = q.value(0).toString().mid(prefix.size()).toInt(&ok);
        if (ok) next = used + 1;
    }
    return prefix + QStringLiteral("%1").arg(next, 3, 10, QLatin1Char('0'));
}

} // namespace

OrderRepository::OrderRepository() : Repository(QStringLiteral("orders")) {}

int OrderRepository::insertOrder(models::Order& o) {
    Database& db = Database::instance();
    int newId = 0;

    db.transaction([&] {
        QDateTime created = o.createdAt();
        if (!created.isValid()) {
            created = QDateTime::currentDateTimeUtc();
            o.setCreatedAt(created);
        }
        if (o.orderNumber().isEmpty())
            o.setOrderNumber(nextOrderNumber(created.toUTC().date()));

        QSqlQuery q = db.prepared(
            QStringLiteral("INSERT INTO orders "
                           "(order_number, type, status, table_id, customer_id, waiter_id, "
                           " note, created_at) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"),
            { o.orderNumber(), models::toString(o.type()), models::toString(o.status()),
              fkOrNull(o.tableId()), fkOrNull(o.customerId()), fkOrNull(o.waiterId()),
              o.note(), tsToDb(created) });

        newId = q.lastInsertId().toInt();
        o.setId(newId);        // the order learns its own identity before its lines reference it
        writeItems(db, o);
    });

    return newId;
}

void OrderRepository::updateOrder(const models::Order& o) {
    Database& db = Database::instance();
    // `order_number` and `created_at` are identity, not editable state, so they are never rewritten:
    // a bill that has been handed to a guest keeps the number printed on it.
    //
    // Lines are replaced wholesale rather than diffed. An OrderItem carries no database id — it is a
    // value, which is what makes split and merge ordinary C++ — so "which row is this line?" is a
    // question the model cannot answer. Delete-then-insert is therefore the honest write, and it
    // removes deleted lines for free.
    db.transaction([&] {
        db.prepared(
            QStringLiteral("UPDATE orders SET type = ?, status = ?, table_id = ?, customer_id = ?, "
                           "waiter_id = ?, note = ? WHERE id = ?"),
            { models::toString(o.type()), models::toString(o.status()),
              fkOrNull(o.tableId()), fkOrNull(o.customerId()), fkOrNull(o.waiterId()),
              o.note(), o.id() });

        db.prepared(QStringLiteral("DELETE FROM order_items WHERE order_id = ?"), { o.id() });
        writeItems(db, o);
    });
}

void OrderRepository::updateStatus(int orderId, models::OrderStatus s) {
    // The cheap path the kitchen board hammers: one column, no lines touched, no transaction needed.
    Database::instance().prepared(
        QStringLiteral("UPDATE orders SET status = ? WHERE id = ?"),
        { models::toString(s), orderId });
}

std::optional<models::Order> OrderRepository::loadOrder(int orderId) const {
    std::optional<models::Order> o = findById(orderId);
    if (!o) return std::nullopt;      // not found is an answer, not a failure
    readItems(*o);
    return o;
}

std::vector<models::Order> OrderRepository::withStatus(models::OrderStatus s) const {
    std::vector<models::Order> out;
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM orders WHERE status = ? ORDER BY created_at DESC"),
        { models::toString(s) });
    while (q.next()) out.push_back(fromRecord(q.record()));
    for (models::Order& o : out) readItems(o);   // headers first, then lines: one live query at a time
    return out;
}

std::vector<models::Order> OrderRepository::activeOrders() const {
    std::vector<models::Order> out;
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM orders WHERE status NOT IN (?, ?) ORDER BY created_at DESC"),
        { models::toString(models::OrderStatus::Paid),
          models::toString(models::OrderStatus::Cancelled) });
    while (q.next()) out.push_back(fromRecord(q.record()));
    for (models::Order& o : out) readItems(o);
    return out;
}

std::vector<models::Order> OrderRepository::between(const QDateTime& from,
                                                    const QDateTime& to) const {
    std::vector<models::Order> out;
    // Half-open [from, to): consecutive report windows tile the timeline without double-counting the
    // order that happens to fall exactly on a boundary.
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM orders WHERE created_at >= ? AND created_at < ? "
                       "ORDER BY created_at DESC"),
        { tsToDb(from), tsToDb(to) });
    while (q.next()) out.push_back(fromRecord(q.record()));
    for (models::Order& o : out) readItems(o);
    return out;
}

std::vector<models::Order> OrderRepository::forCustomer(int customerId, int limit) const {
    std::vector<models::Order> out;
    if (customerId <= 0 || limit <= 0) return out;
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM orders WHERE customer_id = ? ORDER BY created_at DESC LIMIT ?"),
        { customerId, limit });
    while (q.next()) out.push_back(fromRecord(q.record()));
    for (models::Order& o : out) readItems(o);   // favourites are counted from the lines
    return out;
}

void OrderRepository::markMergedInto(int sourceOrderId, int targetOrderId) {
    // The merge trail is a pointer, not a deletion: the absorbed order stays queryable, so a bill
    // that vanished from the floor can still be explained months later.
    Database::instance().prepared(
        QStringLiteral("UPDATE orders SET merged_into = ? WHERE id = ?"),
        { fkOrNull(targetOrderId), sourceOrderId });
}

models::Order OrderRepository::fromRecord(const QSqlRecord& rec) const {
    models::Order o(rec.value(QStringLiteral("id")).toInt(),
                    rec.value(QStringLiteral("order_number")).toString(),
                    models::orderTypeFromString(rec.value(QStringLiteral("type")).toString()),
                    rec.value(QStringLiteral("table_id")).toInt(),
                    rec.value(QStringLiteral("customer_id")).toInt(),
                    rec.value(QStringLiteral("waiter_id")).toInt(),
                    tsFromDb(rec.value(QStringLiteral("created_at")).toString()));
    o.setNote(rec.value(QStringLiteral("note")).toString());
    hydrateStatus(o, models::orderStatusFromString(rec.value(QStringLiteral("status")).toString()));
    return o;   // header only — loadOrder() and the list queries attach the lines
}

QString OrderRepository::orderByClause() const { return QStringLiteral("created_at DESC"); }

} // namespace aluchop::persistence

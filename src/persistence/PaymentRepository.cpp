/**
 * @file PaymentRepository.cpp
 * @brief The `payments` table and the revenue aggregates computed from it.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Every amount is an INTEGER paisa column, so `SUM()` is exact integer arithmetic and the figure is
 * wrapped in `core::Money` the instant it leaves SQL. No revenue number in this application ever
 * passes through a floating-point type, which is why a month of takings can be added up a thousand
 * times and still come back to the same paisa.
 *
 * Every aggregate here reads one half-open window `[from, to)` of the `paid_at` column. That column
 * is stored at **one-second resolution**, so the bounds are snapped onto the same grid before they
 * reach SQL — see `dbWindow()`, which is the single place that knows this and the reason a sale
 * settled a heartbeat ago still shows up in a report asked for "now".
 */

#include "aluchop/persistence/PaymentRepository.hpp"

#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>

#include "aluchop/models/Enums.hpp"
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

QVariant fkOrNull(int id) { return id > 0 ? QVariant(id) : QVariant(); }

/**
 * @brief A `[from, to)` window rendered as the two ISO strings SQL compares `paid_at` against.
 *
 * This tiny struct exists so that `between`, `revenueBetween` and `popularItems` cannot drift apart:
 * all three ask the same question of the same column, so all three must build their bounds the same
 * way.
 */
struct DbWindow {
    bool valid = false;   ///< false ⇒ the caller handed us a window that cannot be queried
    QString from;         ///< inclusive lower bound, ISO-8601 UTC
    QString to;           ///< exclusive upper bound, ISO-8601 UTC
};

/**
 * @brief Renders a half-open `[from, to)` window onto the whole-second grid `paid_at` lives on.
 *
 * `paid_at` is stored by `tsToDb()` — and, when a row is inserted without it, by the column's
 * `strftime('%Y-%m-%dT%H:%M:%S','now')` default — with a resolution of **one second**; the
 * sub-second part of the payment instant is thrown away. The comparison in SQL is therefore a
 * comparison between second-resolution strings, and a bound that falls part-way through a second
 * does not mean what it looks like: a sale settled at 12:00:00.400 is on disk as `12:00:00`, so the
 * exclusive bound `12:00:00.912` — which truncates to the *same* `12:00:00` — excludes it. With
 * "now" as the upper bound that is precisely how every sale made during the current second vanishes
 * from a report.
 *
 * The fix is to snap the bounds outwards onto the grid the data actually sits on:
 *   - the inclusive lower bound truncates down (what formatting already does), and
 *   - the exclusive upper bound is raised to the **start of the next second** whenever it carries a
 *     sub-second remainder, so the second it falls inside is covered in full.
 *
 * The window stays half-open, so nothing is double counted; and every window this application really
 * asks for (midnights, month starts, and now-bounded trailing windows built by `ReportService`) is
 * already second-aligned, which makes the snap a no-op for them and leaves adjacent day windows
 * exactly as disjoint as they were.
 *
 * An invalid bound is refused rather than formatted: `QDateTime().toString()` is the empty string,
 * and `paid_at >= ''` is true of every row ever written — an invalid `from` would silently turn a
 * one-day query into "all revenue since the restaurant opened".
 */
DbWindow dbWindow(const QDateTime& from, const QDateTime& to) {
    DbWindow w;
    if (!from.isValid() || !to.isValid())
        return w;

    QDateTime upper = to.toUTC();
    if (const int ms = upper.time().msec(); ms != 0)
        upper = upper.addMSecs(1000 - ms);   // ceil onto the second `paid_at` is stored at

    w.from = tsToDb(from);
    w.to = tsToDb(upper);
    w.valid = true;
    return w;
}

} // namespace

PaymentRepository::PaymentRepository() : Repository(QStringLiteral("payments")) {}

int PaymentRepository::insert(const models::Payment& p) {
    const QDateTime paidAt = p.paidAt().isValid() ? p.paidAt() : QDateTime::currentDateTimeUtc();
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("INSERT INTO payments "
                       "(order_id, method, subtotal_paisa, discount_paisa, service_charge_paisa, "
                       " total_paisa, tendered_paisa, change_paisa, promo_id, received_by, paid_at) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
        { p.orderId(), models::toString(p.method()),
          static_cast<qlonglong>(p.subtotal().paisa()),
          static_cast<qlonglong>(p.discount().paisa()),
          static_cast<qlonglong>(p.serviceCharge().paisa()),
          static_cast<qlonglong>(p.total().paisa()),
          static_cast<qlonglong>(p.tendered().paisa()),
          static_cast<qlonglong>(p.change().paisa()),
          fkOrNull(p.promoId()), fkOrNull(p.receivedByUserId()), tsToDb(paidAt) });
    return q.lastInsertId().toInt();
}

std::vector<models::Payment> PaymentRepository::between(const QDateTime& from,
                                                        const QDateTime& to) const {
    std::vector<models::Payment> out;
    const DbWindow w = dbWindow(from, to);
    if (!w.valid) return out;

    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM payments WHERE paid_at >= ? AND paid_at < ? "
                       "ORDER BY paid_at DESC"),
        { w.from, w.to });
    while (q.next()) out.push_back(fromRecord(q.record()));
    return out;
}

core::Money PaymentRepository::revenueBetween(const QDateTime& from, const QDateTime& to) const {
    const DbWindow w = dbWindow(from, to);
    if (!w.valid) return core::Money::zero();

    // COALESCE turns "no payments in this window" into an honest zero rather than a NULL that would
    // silently become 0 anyway — stating it makes the intent readable in the statement itself.
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT COALESCE(SUM(total_paisa), 0) FROM payments "
                       "WHERE paid_at >= ? AND paid_at < ?"),
        { w.from, w.to });
    if (q.next()) return core::Money(q.value(0).toLongLong());
    return core::Money::zero();
}

std::vector<std::pair<QString, int>> PaymentRepository::popularItems(const QDateTime& from,
                                                                     const QDateTime& to,
                                                                     int topN) const {
    std::vector<std::pair<QString, int>> out;
    if (topN <= 0) return out;

    const DbWindow w = dbWindow(from, to);
    if (!w.valid) return out;

    // Ranked over *settled* orders only — an abandoned or cancelled order never sold anything.
    // Grouping by the line's name snapshot rather than by menu_item_id keeps a dish that was later
    // deleted from the menu visible in historical reports instead of collapsing into one NULL bucket.
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT oi.name_snapshot, SUM(oi.qty) AS sold "
                       "FROM order_items oi "
                       "JOIN payments p ON p.order_id = oi.order_id "
                       "WHERE p.paid_at >= ? AND p.paid_at < ? "
                       "GROUP BY oi.name_snapshot "
                       "ORDER BY sold DESC, oi.name_snapshot ASC LIMIT ?"),
        { w.from, w.to, topN });

    while (q.next()) out.emplace_back(q.value(0).toString(), q.value(1).toInt());
    return out;
}

models::Payment PaymentRepository::fromRecord(const QSqlRecord& rec) const {
    models::Payment p;
    p.setId(rec.value(QStringLiteral("id")).toInt());
    p.setOrderId(rec.value(QStringLiteral("order_id")).toInt());
    p.setMethod(models::paymentMethodFromString(rec.value(QStringLiteral("method")).toString()));
    p.setSubtotal(core::Money(rec.value(QStringLiteral("subtotal_paisa")).toLongLong()));
    p.setDiscount(core::Money(rec.value(QStringLiteral("discount_paisa")).toLongLong()));
    p.setServiceCharge(core::Money(rec.value(QStringLiteral("service_charge_paisa")).toLongLong()));
    p.setTotal(core::Money(rec.value(QStringLiteral("total_paisa")).toLongLong()));
    p.setTendered(core::Money(rec.value(QStringLiteral("tendered_paisa")).toLongLong()));
    p.setChange(core::Money(rec.value(QStringLiteral("change_paisa")).toLongLong()));
    p.setPromoId(rec.value(QStringLiteral("promo_id")).toInt());
    p.setReceivedByUserId(rec.value(QStringLiteral("received_by")).toInt());
    p.setPaidAt(tsFromDb(rec.value(QStringLiteral("paid_at")).toString()));
    return p;
}

QString PaymentRepository::orderByClause() const { return QStringLiteral("paid_at DESC"); }

} // namespace aluchop::persistence

/**
 * @file PaymentRepository.cpp
 * @brief The `payments` table and the revenue aggregates computed from it.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Every amount is an INTEGER paisa column, so `SUM()` is exact integer arithmetic and the figure is
 * wrapped in `core::Money` the instant it leaves SQL. No revenue number in this application ever
 * passes through a floating-point type, which is why a month of takings can be added up a thousand
 * times and still come back to the same paisa.
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
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM payments WHERE paid_at >= ? AND paid_at < ? "
                       "ORDER BY paid_at DESC"),
        { tsToDb(from), tsToDb(to) });
    while (q.next()) out.push_back(fromRecord(q.record()));
    return out;
}

core::Money PaymentRepository::revenueBetween(const QDateTime& from, const QDateTime& to) const {
    // COALESCE turns "no payments in this window" into an honest zero rather than a NULL that would
    // silently become 0 anyway — stating it makes the intent readable in the statement itself.
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT COALESCE(SUM(total_paisa), 0) FROM payments "
                       "WHERE paid_at >= ? AND paid_at < ?"),
        { tsToDb(from), tsToDb(to) });
    if (q.next()) return core::Money(q.value(0).toLongLong());
    return core::Money::zero();
}

std::vector<std::pair<QString, int>> PaymentRepository::popularItems(const QDateTime& from,
                                                                     const QDateTime& to,
                                                                     int topN) const {
    std::vector<std::pair<QString, int>> out;
    if (topN <= 0) return out;

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
        { tsToDb(from), tsToDb(to), topN });

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

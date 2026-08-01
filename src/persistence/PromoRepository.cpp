/**
 * @file PromoRepository.cpp
 * @brief CRUD and code lookup for the `promos` table.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * This repository stores and retrieves promos; it never decides whether one may be used. That rule
 * lives in `models::Promo::isValidOn()` so the till and the reports cannot disagree about it.
 */

#include "aluchop/persistence/PromoRepository.hpp"

#include <QDate>
#include <QSqlQuery>
#include <QVariant>

#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {
namespace {

/// Dates are stored as TEXT `yyyy-MM-dd`; an open-ended bound is a genuine SQL NULL, not "0000-00-00".
QVariant dateOrNull(QDate d) {
    return d.isValid() ? QVariant(d.toString(QStringLiteral("yyyy-MM-dd"))) : QVariant();
}

QDate dateFromDb(const QVariant& v) {
    if (v.isNull()) return QDate();
    return QDate::fromString(v.toString(), QStringLiteral("yyyy-MM-dd"));
}

} // namespace

PromoRepository::PromoRepository() : Repository(QStringLiteral("promos")) {}

int PromoRepository::insert(const models::Promo& p) {
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("INSERT INTO promos "
                       "(code, kind, percent, flat_paisa, min_order_paisa, valid_from, valid_to, is_active) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"),
        { p.code(), models::toString(p.kind()), p.percent(),
          static_cast<qlonglong>(p.flatAmount().paisa()),
          static_cast<qlonglong>(p.minOrder().paisa()),
          dateOrNull(p.validFrom()), dateOrNull(p.validTo()), p.isActive() ? 1 : 0 });
    return q.lastInsertId().toInt();
}

void PromoRepository::update(const models::Promo& p) {
    Database::instance().prepared(
        QStringLiteral("UPDATE promos SET code = ?, kind = ?, percent = ?, flat_paisa = ?, "
                       "min_order_paisa = ?, valid_from = ?, valid_to = ?, is_active = ? WHERE id = ?"),
        { p.code(), models::toString(p.kind()), p.percent(),
          static_cast<qlonglong>(p.flatAmount().paisa()),
          static_cast<qlonglong>(p.minOrder().paisa()),
          dateOrNull(p.validFrom()), dateOrNull(p.validTo()), p.isActive() ? 1 : 0, p.id() });
}

std::optional<models::Promo> PromoRepository::byCode(const QString& code) const {
    if (code.trimmed().isEmpty()) return std::nullopt;
    // COLLATE NOCASE makes "welcome10" find WELCOME10 without ever concatenating the code into SQL.
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM promos WHERE code = ? COLLATE NOCASE LIMIT 1"),
        { code.trimmed() });
    if (q.next()) return fromRecord(q.record());
    return std::nullopt;
}

models::Promo PromoRepository::fromRecord(const QSqlRecord& rec) const {
    models::Promo p;
    p.setId(rec.value(QStringLiteral("id")).toInt());
    p.setCode(rec.value(QStringLiteral("code")).toString());
    p.setKind(models::promoKindFromString(rec.value(QStringLiteral("kind")).toString()));
    p.setPercent(rec.value(QStringLiteral("percent")).toInt());
    p.setFlatAmount(core::Money(rec.value(QStringLiteral("flat_paisa")).toLongLong()));
    p.setMinOrder(core::Money(rec.value(QStringLiteral("min_order_paisa")).toLongLong()));
    p.setValidFrom(dateFromDb(rec.value(QStringLiteral("valid_from"))));
    p.setValidTo(dateFromDb(rec.value(QStringLiteral("valid_to"))));
    p.setActive(rec.value(QStringLiteral("is_active")).toInt() != 0);
    return p;
}

QString PromoRepository::orderByClause() const { return QStringLiteral("code"); }

} // namespace aluchop::persistence

/**
 * @file AuditRepository.cpp
 * @brief SQLite mirror of the fixed-record binary audit trail.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The authoritative record is `AuditTrail`'s binary file; this table exists purely so the trail can
 * be *queried*. Both carry the same `seq` number, which is exactly what lets a reviewer line the two
 * up and notice if one of them has been edited behind the application's back.
 *
 * Rows are only ever appended and read — there is no update and no delete, by design.
 */

#include "aluchop/persistence/AuditRepository.hpp"

#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>

#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {

void AuditRepository::insert(quint32 seq, qint64 tsUtcMs, int userId, const QString& action,
                             const QString& entity, core::Money amount, const QString& details) {
    // `amount` travels as INTEGER paisa: exactly the integer core::Money already holds, with no
    // floating-point step anywhere between the model and the column.
    Database::instance().prepared(
        QStringLiteral("INSERT INTO audit_log "
                       "(seq, ts_utc_ms, user_id, action, entity, amount_paisa, details) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?)"),
        { static_cast<qlonglong>(seq), static_cast<qlonglong>(tsUtcMs), userId, action, entity,
          static_cast<qlonglong>(amount.paisa()), details });
}

std::vector<std::tuple<quint32, QDateTime, int, QString, QString, core::Money, QString>>
AuditRepository::recent(int limit) const {
    std::vector<std::tuple<quint32, QDateTime, int, QString, QString, core::Money, QString>> out;
    if (limit <= 0) return out;

    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT seq, ts_utc_ms, user_id, action, entity, amount_paisa, details "
                       "FROM audit_log ORDER BY id DESC LIMIT ?"),
        { limit });

    while (q.next()) {
        out.emplace_back(static_cast<quint32>(q.value(0).toUInt()),
                         QDateTime::fromMSecsSinceEpoch(q.value(1).toLongLong(), QTimeZone::UTC),
                         q.value(2).toInt(),
                         q.value(3).toString(),
                         q.value(4).toString(),
                         core::Money(q.value(5).toLongLong()),
                         q.value(6).toString());
    }
    return out;
}

} // namespace aluchop::persistence

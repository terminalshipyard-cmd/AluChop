/**
 * @file ReservationRepository.cpp
 * @brief CRUD and overlap queries for the `reservations` table.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Timestamps are stored as ISO-8601 UTC text, so ordinary string comparison in SQL is also correct
 * chronological comparison — no date arithmetic in SQL, and no dependency on SQLite's date functions.
 */

#include "aluchop/persistence/ReservationRepository.hpp"

#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>

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

ReservationRepository::ReservationRepository() : Repository(QStringLiteral("reservations")) {}

int ReservationRepository::insert(const models::Reservation& r) {
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("INSERT INTO reservations "
                       "(customer_id, customer_name, phone, table_id, starts_at, duration_min, "
                       " guests, special_request, status) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"),
        { fkOrNull(r.customerId()), r.customerName(), r.phone(), r.tableId(),
          tsToDb(r.startsAt()), r.durationMin(), r.guests(), r.specialRequest(),
          models::toString(r.status()) });
    return q.lastInsertId().toInt();
}

void ReservationRepository::update(const models::Reservation& r) {
    Database::instance().prepared(
        QStringLiteral("UPDATE reservations SET customer_id = ?, customer_name = ?, phone = ?, "
                       "table_id = ?, starts_at = ?, duration_min = ?, guests = ?, "
                       "special_request = ?, status = ? WHERE id = ?"),
        { fkOrNull(r.customerId()), r.customerName(), r.phone(), r.tableId(),
          tsToDb(r.startsAt()), r.durationMin(), r.guests(), r.specialRequest(),
          models::toString(r.status()), r.id() });
}

void ReservationRepository::setStatus(int reservationId, models::ReservationStatus s) {
    Database::instance().prepared(
        QStringLiteral("UPDATE reservations SET status = ? WHERE id = ?"),
        { models::toString(s), reservationId });
}

std::vector<models::Reservation> ReservationRepository::onDay(QDate day) const {
    // `day` is the restaurant's *local* calendar day — that is what a date picker means — so the two
    // bounds are local midnights converted to the UTC text actually stored in the column.
    std::vector<models::Reservation> out;
    const QDateTime dayStart(day, QTime(0, 0));
    const QDateTime dayEnd = dayStart.addDays(1);

    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM reservations WHERE starts_at >= ? AND starts_at < ? "
                       "ORDER BY starts_at"),
        { tsToDb(dayStart), tsToDb(dayEnd) });
    while (q.next()) out.push_back(fromRecord(q.record()));
    return out;
}

std::vector<models::Reservation> ReservationRepository::overlapping(int tableId,
                                                                    const QDateTime& start,
                                                                    int durationMin) const {
    std::vector<models::Reservation> out;
    const QDateTime proposedEnd = start.addSecs(60LL * durationMin);

    // SQL narrows to the only rows that *could* clash: the same table, a status that still holds the
    // table, and a booking that begins before the proposed window ends. The precise half-open test is
    // then delegated to models::Reservation::overlaps(), which is the single definition of "clash" in
    // the whole application — duplicating it as SQL date arithmetic is how the two would drift apart.
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM reservations WHERE table_id = ? AND status IN (?, ?) "
                       "AND starts_at < ? ORDER BY starts_at"),
        { tableId,
          models::toString(models::ReservationStatus::Booked),
          models::toString(models::ReservationStatus::Seated),
          tsToDb(proposedEnd) });

    while (q.next()) {
        models::Reservation r = fromRecord(q.record());
        if (r.overlaps(start, durationMin)) out.push_back(r);
    }
    return out;
}

models::Reservation ReservationRepository::fromRecord(const QSqlRecord& rec) const {
    models::Reservation r(rec.value(QStringLiteral("id")).toInt(),
                          rec.value(QStringLiteral("table_id")).toInt(),
                          rec.value(QStringLiteral("customer_name")).toString(),
                          rec.value(QStringLiteral("phone")).toString(),
                          tsFromDb(rec.value(QStringLiteral("starts_at")).toString()),
                          rec.value(QStringLiteral("duration_min")).toInt(),
                          rec.value(QStringLiteral("guests")).toInt());
    r.setCustomerId(rec.value(QStringLiteral("customer_id")).toInt());
    r.setSpecialRequest(rec.value(QStringLiteral("special_request")).toString());
    r.setStatus(models::reservationStatusFromString(rec.value(QStringLiteral("status")).toString()));
    return r;
}

QString ReservationRepository::orderByClause() const { return QStringLiteral("starts_at"); }

} // namespace aluchop::persistence

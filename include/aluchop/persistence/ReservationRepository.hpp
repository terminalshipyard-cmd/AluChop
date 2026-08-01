#pragma once

/**
 * @file ReservationRepository.hpp
 * @brief CRUD and overlap queries for the `reservations` table.
 */

#include <vector>

#include <QDate>
#include <QDateTime>
#include <QSqlRecord>
#include <QString>

#include "aluchop/models/Enums.hpp"
#include "aluchop/models/Reservation.hpp"
#include "aluchop/persistence/Repository.hpp"

namespace aluchop::persistence {

/// @brief Repository over `reservations`; the overlap query is what makes double-booking impossible.
class ReservationRepository : public Repository<models::Reservation> {
public:
    ReservationRepository();

    /// @brief Inserts a booking. @return the generated primary key.
    int insert(const models::Reservation& r);

    /// @brief Rewrites every column of an existing booking.
    void update(const models::Reservation& r);

    /// @brief Status-only update (seat / complete / cancel / no-show).
    void setStatus(int reservationId, models::ReservationStatus s);

    /// @return every booking whose start falls on @p day, earliest first.
    std::vector<models::Reservation> onDay(QDate day) const;

    /**
     * @brief Bookings of one table that clash with the half-open window
     *        `[start, start + durationMin)`.
     * @note Only `BOOKED` and `SEATED` rows can clash — cancelled and completed ones free the slot.
     */
    std::vector<models::Reservation> overlapping(int tableId, const QDateTime& start,
                                                 int durationMin) const;

protected:
    models::Reservation fromRecord(const QSqlRecord& rec) const override;

    /// @return `"starts_at"` — the day view is chronological.
    QString orderByClause() const override;
};

} // namespace aluchop::persistence

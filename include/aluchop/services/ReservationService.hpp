#pragma once

/**
 * @file ReservationService.hpp
 * @brief Table bookings: availability, booking, seating and cancellation.
 *
 * Availability is computed rather than stored: a table is free for a window when it is active,
 * large enough for the party, and has no `BOOKED`/`SEATED` reservation overlapping that window.
 */

#include <vector>

#include <QDate>
#include <QDateTime>

#include "aluchop/core/Result.hpp"
#include "aluchop/models/Reservation.hpp"
#include "aluchop/models/Table.hpp"
#include "aluchop/persistence/CustomerRepository.hpp"
#include "aluchop/persistence/ReservationRepository.hpp"
#include "aluchop/persistence/TableRepository.hpp"

namespace aluchop::services {

class AuditService;
class NotificationService;

/// @brief The booking book.
class ReservationService {
public:
    ReservationService(persistence::ReservationRepository& reservations,
                       persistence::TableRepository& tables,
                       persistence::CustomerRepository& customers,
                       AuditService& audit, NotificationService& notify);

    /**
     * @brief Tables that can take a party of @p guests for the window starting at @p start.
     * @details Active tables with enough capacity, minus those with an overlapping live booking.
     */
    std::vector<models::Table> availableTables(const QDateTime& start, int durationMin,
                                               int guests) const;

    /// @brief Books a table. @return the new reservation id, or an error (slot taken, time in the past).
    core::Result<int> book(const models::Reservation& r);

    /// @brief Saves an edited booking, re-checking availability for the new window.
    core::Result<void> update(const models::Reservation& r);

    /// @brief Cancels a booking, freeing its slot.
    core::Result<void> cancel(int reservationId);

    /// @brief `Booked -> Seated`: the party has arrived.
    core::Result<void> seat(int reservationId);

    /// @brief `Seated -> Completed`: the table has turned over.
    core::Result<void> complete(int reservationId);

    /// @return every booking that starts on @p day, chronologically.
    std::vector<models::Reservation> onDay(QDate day) const;

    /// @return every table in the restaurant (for the picker and the floor view).
    std::vector<models::Table> tables() const;

private:
    persistence::ReservationRepository& m_reservations;
    persistence::TableRepository& m_tables;
    persistence::CustomerRepository& m_customers;
    AuditService& m_audit;
    NotificationService& m_notify;
};

} // namespace aluchop::services

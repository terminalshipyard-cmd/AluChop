#pragma once

/// \file
/// \brief Reservation — a table booked for a time window.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QDateTime>
#include <QString>

#include "aluchop/models/Enums.hpp"

namespace aluchop::models {

/// \brief A booking: which table, from when, for how long, for how many guests.
///
/// The guest's name and phone are stored on the booking itself rather than only
/// as a foreign key, because most bookings come from walk-in callers who are not
/// in the loyalty database. customerId() is filled in only when the caller is
/// recognised.
class Reservation {
public:
    /// \brief Default constructor — used by ReservationRepository before hydration.
    Reservation() = default;

    /// @oop-concept Parameterised Constructor :: a booking is meaningless without a table and a time
    /// \param id           Database primary key, 0 when not yet persisted.
    /// \param tableId      Table being booked.
    /// \param customerName Guest's name; must not be blank.
    /// \param phone        Guest's contact number.
    /// \param startsAt     Start of the booking (UTC).
    /// \param durationMin  Length in minutes; must be at least 15.
    /// \param guests       Party size; must be at least 1.
    /// \throws core::ValidationException when a field fails its setter rule.
    Reservation(int id, int tableId, QString customerName, QString phone,
                QDateTime startsAt, int durationMin, int guests);

    /// \brief Database primary key.
    int id() const noexcept { return m_id; }
    /// \brief Assign the primary key after an INSERT.
    void setId(int v) { m_id = v; }

    /// \brief Table being held.
    int tableId() const noexcept { return m_tableId; }
    /// \brief Set the table being held.
    void setTableId(int v) { m_tableId = v; }

    /// \brief Loyalty customer, or 0 when the caller is not in the database.
    int customerId() const noexcept { return m_customerId; }
    /// \brief Link the booking to a loyalty customer.
    void setCustomerId(int v) { m_customerId = v; }

    /// \brief Guest's name as given on the phone.
    const QString& customerName() const noexcept { return m_customerName; }
    /// \brief Set the guest's name.
    /// \throws core::ValidationException when \p v trims to empty.
    void setCustomerName(const QString& v);

    /// \brief Guest's contact number.
    const QString& phone() const noexcept { return m_phone; }
    /// \brief Set the contact number.
    void setPhone(const QString& v) { m_phone = v; }

    /// \brief Start of the booking (UTC).
    QDateTime startsAt() const noexcept { return m_startsAt; }
    /// \brief Set the start of the booking.
    void setStartsAt(QDateTime t) { m_startsAt = t; }

    /// \brief Length of the booking in minutes.
    int durationMin() const noexcept { return m_durationMin; }
    /// \brief Set the length of the booking.
    /// \throws core::ValidationException when \p v is below 15 minutes.
    void setDurationMin(int v);

    /// @oop-concept Inline Functions :: derived from start + duration, never stored,
    /// so the two can never disagree
    /// \brief End of the booking (UTC).
    QDateTime endsAt() const { return m_startsAt.addSecs(60LL * m_durationMin); }

    /// \brief Party size.
    int guests() const noexcept { return m_guests; }
    /// \brief Set the party size.
    /// \throws core::ValidationException when \p v is below 1.
    void setGuests(int v);

    /// \brief Free-text request, e.g. "window seat, birthday cake".
    const QString& specialRequest() const noexcept { return m_specialRequest; }
    /// \brief Set the special request.
    void setSpecialRequest(const QString& v) { m_specialRequest = v; }

    /// \brief Where the booking stands.
    ReservationStatus status() const noexcept { return m_status; }
    /// \brief Set the booking status.
    void setStatus(ReservationStatus s) { m_status = s; }

    /// @oop-concept Constant Member Functions :: the availability check asks, never mutates
    /// \brief Whether this booking collides with the proposed window.
    ///
    /// The comparison is half-open — `[start, end)` — so a booking that ends at
    /// 19:00 does not block one that starts at 19:00. Without that, every table
    /// would appear double-booked on the hour.
    /// \param start       Start of the proposed window.
    /// \param durationMin Length of the proposed window in minutes.
    /// \return `true` when the two windows overlap.
    bool overlaps(const QDateTime& start, int durationMin) const;

private:
    int m_id = 0;             ///< Primary key.
    int m_tableId = 0;        ///< Table being held.
    int m_customerId = 0;     ///< Loyalty customer, or 0.
    int m_durationMin = 90;   ///< House default sitting: 90 minutes.
    int m_guests = 1;         ///< Party size.
    QString m_customerName;   ///< Guest's name.
    QString m_phone;          ///< Guest's contact number.
    QString m_specialRequest; ///< Free-text request.
    QDateTime m_startsAt;     ///< Start of the booking (UTC).
    ReservationStatus m_status = ReservationStatus::Booked; ///< Booking status.
};

} // namespace aluchop::models

/// \file
/// \brief Implementation of models::Reservation.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Reservation.hpp"

#include <string>
#include <utility>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::models {

Reservation::Reservation(int id, int tableId, QString customerName, QString phone,
                         QDateTime startsAt, int durationMin, int guests)
    : m_id(id)
    , m_tableId(tableId)
    , m_startsAt(std::move(startsAt))
{
    setCustomerName(customerName);
    setPhone(phone.trimmed());
    setDurationMin(durationMin);
    setGuests(guests);
}

void Reservation::setCustomerName(const QString& v)
{
    const QString trimmed = v.trimmed();
    if (trimmed.isEmpty())
        throw core::ValidationException("Reservation must record a guest name");
    m_customerName = trimmed;
}

void Reservation::setDurationMin(int v)
{
    // Fifteen minutes is the shortest sitting the floor plan can service; below
    // that the booking is a mistake, not a short lunch.
    if (v < 15)
        throw core::ValidationException(
            "Reservation duration must be at least 15 minutes (got " + std::to_string(v) + ")");
    m_durationMin = v;
}

void Reservation::setGuests(int v)
{
    if (v < 1)
        throw core::ValidationException(
            "A reservation must be for at least 1 guest (got " + std::to_string(v) + ")");
    m_guests = v;
}

bool Reservation::overlaps(const QDateTime& start, int durationMin) const
{
    // A cancelled, completed or no-show booking releases the table, so it cannot
    // collide with anything.
    if (m_status == ReservationStatus::Cancelled || m_status == ReservationStatus::Completed
        || m_status == ReservationStatus::NoShow)
        return false;

    if (!m_startsAt.isValid() || !start.isValid())
        return false;

    const QDateTime otherEnd = start.addSecs(60LL * static_cast<qint64>(durationMin));
    const QDateTime thisEnd = endsAt();

    // Half-open intervals [start, end): a booking that ends at 19:00 leaves the
    // table free for one that starts at 19:00. With closed intervals every table
    // would look double-booked on the hour.
    return m_startsAt < otherEnd && start < thisEnd;
}

} // namespace aluchop::models

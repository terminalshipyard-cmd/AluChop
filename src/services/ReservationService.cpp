/**
 * @file ReservationService.cpp
 * @brief Implementation of the booking book: availability, booking, seating and cancellation.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Availability is **computed, never stored**. There is no "is_free" column that could drift out of
 * step with reality: a table is free for a window when it is active, large enough for the party and
 * has no `Booked`/`Seated` reservation overlapping that window. Because the overlap test is
 * half-open — `[start, end)` — a booking that ends at 19:00 does not block one that starts at
 * 19:00, which is the difference between a usable book and one where every table looks taken on
 * the hour.
 */

#include "aluchop/services/ReservationService.hpp"

#include <algorithm>
#include <vector>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Logger.hpp"
#include "aluchop/models/Customer.hpp"
#include "aluchop/services/AuditService.hpp"
#include "aluchop/services/NotificationService.hpp"

namespace aluchop::services {

namespace {

/// Shortest sitting the house will take, mirroring `CHECK (duration_min >= 15)`.
constexpr int kMinDurationMin = 15;

/// A booking may be entered a few minutes late (the phone call ran on) but not for last week.
constexpr qint64 kPastGraceSecs = 15 * 60;

QString reservationTag(int id) {
    return QStringLiteral("resv:%1").arg(id);
}

QString exceptionText(const core::AluChopException& ex) {
    return QString::fromUtf8(ex.what());
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

/// @oop-concept Pass by Reference :: repositories are owned by AppContext and borrowed here.
ReservationService::ReservationService(persistence::ReservationRepository& reservations,
                                       persistence::TableRepository& tables,
                                       persistence::CustomerRepository& customers,
                                       AuditService& audit, NotificationService& notify)
    : m_reservations(reservations),
      m_tables(tables),
      m_customers(customers),
      m_audit(audit),
      m_notify(notify) {}

// ---------------------------------------------------------------------------
// Availability
// ---------------------------------------------------------------------------

std::vector<models::Table> ReservationService::availableTables(const QDateTime& start,
                                                               int durationMin, int guests) const {
    if (!start.isValid() || durationMin < kMinDurationMin || guests < 1)
        return {};

    try {
        // Start from the tables that could physically seat the party at all...
        std::vector<models::Table> candidates = m_tables.activeWithCapacityAtLeast(guests);

        /// @oop-concept STL Algorithms / Iterators :: the blocked tables are removed with the
        /// erase-remove idiom, so "available" is expressed as a filter over the candidate set
        /// rather than as a hand-rolled index loop.
        const auto blocked = [&](const models::Table& t) {
            return !m_reservations.overlapping(t.id(), start, durationMin).empty();
        };
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), blocked),
                         candidates.end());

        return candidates;
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("availability could not be computed: %1").arg(exceptionText(ex)));
        return {};
    }
}

// ---------------------------------------------------------------------------
// Booking
// ---------------------------------------------------------------------------

core::Result<int> ReservationService::book(const models::Reservation& r) {
    using R = core::Result<int>;

    if (r.customerName().trimmed().isEmpty())
        return R::err(QStringLiteral("A booking needs a name."));
    if (!r.startsAt().isValid())
        return R::err(QStringLiteral("A booking needs a start time."));
    if (r.durationMin() < kMinDurationMin)
        return R::err(QStringLiteral("A sitting is at least %1 minutes.").arg(kMinDurationMin));
    if (r.guests() < 1)
        return R::err(QStringLiteral("A booking needs at least one guest."));
    if (r.tableId() <= 0)
        return R::err(QStringLiteral("Choose a table for this booking."));
    if (r.startsAt().toUTC().secsTo(QDateTime::currentDateTimeUtc()) > kPastGraceSecs)
        return R::err(QStringLiteral("That time has already passed."));

    try {
        const auto table = m_tables.findById(r.tableId());
        if (!table)
            return R::err(QStringLiteral("That table no longer exists."));
        if (!table->isActive())
            return R::err(QStringLiteral("Table %1 is out of service.").arg(table->name()));
        if (table->capacity() < r.guests())
            return R::err(QStringLiteral("Table %1 seats %2 — too small for %3 guests.")
                              .arg(table->name()).arg(table->capacity()).arg(r.guests()));

        // The double-booking guard. Only Booked and Seated rows can clash; cancelled, completed
        // and no-show bookings have released the slot.
        const auto clashes = m_reservations.overlapping(r.tableId(), r.startsAt(), r.durationMin());
        if (!clashes.empty())
            return R::err(QStringLiteral("Table %1 is already held from %2 until %3.")
                              .arg(table->name(),
                                   clashes.front().startsAt().toLocalTime().toString(
                                       QStringLiteral("d MMM HH:mm")),
                                   clashes.front().endsAt().toLocalTime().toString(
                                       QStringLiteral("HH:mm"))));

        models::Reservation booking = r;

        // A caller the restaurant already knows is linked to their loyalty record, so the booking
        // shows up in their visit history rather than as an anonymous walk-in.
        if (booking.customerId() == 0 && !booking.phone().trimmed().isEmpty()) {
            const auto known = m_customers.byPhone(booking.phone().trimmed());
            if (known) booking.setCustomerId(known->id());
        }

        booking.setStatus(models::ReservationStatus::Booked);
        const int id = m_reservations.insert(booking);

        m_audit.log(QStringLiteral("RESV_NEW"), reservationTag(id));
        m_notify.announceDataChanged(QStringLiteral("reservations"));
        m_notify.notify(QStringLiteral("Table booked"),
                        QStringLiteral("%1 for %2 at %3")
                            .arg(table->name()).arg(booking.guests())
                            .arg(booking.startsAt().toLocalTime().toString(
                                QStringLiteral("d MMM HH:mm"))),
                        1);
        return R::ok(id);
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

core::Result<void> ReservationService::update(const models::Reservation& r) {
    using R = core::Result<void>;

    if (r.id() <= 0)
        return R::err(QStringLiteral("That booking has never been saved."));
    if (r.customerName().trimmed().isEmpty())
        return R::err(QStringLiteral("A booking needs a name."));
    if (!r.startsAt().isValid())
        return R::err(QStringLiteral("A booking needs a start time."));
    if (r.durationMin() < kMinDurationMin)
        return R::err(QStringLiteral("A sitting is at least %1 minutes.").arg(kMinDurationMin));
    if (r.guests() < 1)
        return R::err(QStringLiteral("A booking needs at least one guest."));
    if (r.tableId() <= 0)
        return R::err(QStringLiteral("Choose a table for this booking."));

    try {
        const auto existing = m_reservations.findById(r.id());
        if (!existing)
            return R::err(QStringLiteral("That booking no longer exists."));

        const auto table = m_tables.findById(r.tableId());
        if (!table)
            return R::err(QStringLiteral("That table no longer exists."));
        if (!table->isActive())
            return R::err(QStringLiteral("Table %1 is out of service.").arg(table->name()));
        if (table->capacity() < r.guests())
            return R::err(QStringLiteral("Table %1 seats %2 — too small for %3 guests.")
                              .arg(table->name()).arg(table->capacity()).arg(r.guests()));

        // Availability is re-checked for the NEW window, ignoring this booking's own row —
        // otherwise every edit would report the booking as clashing with itself.
        const auto clashes = m_reservations.overlapping(r.tableId(), r.startsAt(), r.durationMin());
        const auto other = std::find_if(clashes.begin(), clashes.end(),
                                        [&r](const models::Reservation& c) {
                                            return c.id() != r.id();
                                        });
        if (other != clashes.end())
            return R::err(QStringLiteral("Table %1 is already held from %2 until %3.")
                              .arg(table->name(),
                                   other->startsAt().toLocalTime().toString(
                                       QStringLiteral("d MMM HH:mm")),
                                   other->endsAt().toLocalTime().toString(
                                       QStringLiteral("HH:mm"))));

        models::Reservation booking = r;
        if (booking.customerId() == 0 && !booking.phone().trimmed().isEmpty()) {
            const auto known = m_customers.byPhone(booking.phone().trimmed());
            if (known) booking.setCustomerId(known->id());
        }

        m_reservations.update(booking);

        m_audit.log(QStringLiteral("RESV_EDIT"), reservationTag(booking.id()));
        m_notify.announceDataChanged(QStringLiteral("reservations"));
        return R::ok();
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

// ---------------------------------------------------------------------------
// The booking's own little lifecycle
// ---------------------------------------------------------------------------

core::Result<void> ReservationService::cancel(int reservationId) {
    using R = core::Result<void>;

    try {
        const auto booking = m_reservations.findById(reservationId);
        if (!booking)
            return R::err(QStringLiteral("That booking no longer exists."));
        if (booking->status() == models::ReservationStatus::Cancelled)
            return R::err(QStringLiteral("That booking is already cancelled."));
        if (booking->status() == models::ReservationStatus::Completed)
            return R::err(QStringLiteral("That sitting has already finished."));

        m_reservations.setStatus(reservationId, models::ReservationStatus::Cancelled);

        m_audit.log(QStringLiteral("RESV_VOID"), reservationTag(reservationId));
        m_notify.announceDataChanged(QStringLiteral("reservations"));
        m_notify.notify(QStringLiteral("Booking cancelled"),
                        QStringLiteral("%1 — the slot is free again").arg(booking->customerName()),
                        2);
        return R::ok();
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

core::Result<void> ReservationService::seat(int reservationId) {
    using R = core::Result<void>;

    try {
        const auto booking = m_reservations.findById(reservationId);
        if (!booking)
            return R::err(QStringLiteral("That booking no longer exists."));
        if (booking->status() != models::ReservationStatus::Booked)
            return R::err(QStringLiteral("Only a confirmed booking can be seated."));

        m_reservations.setStatus(reservationId, models::ReservationStatus::Seated);

        m_audit.log(QStringLiteral("RESV_SEAT"), reservationTag(reservationId));
        m_notify.announceDataChanged(QStringLiteral("reservations"));
        m_notify.notify(QStringLiteral("Party seated"), booking->customerName(), 1);
        return R::ok();
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

core::Result<void> ReservationService::complete(int reservationId) {
    using R = core::Result<void>;

    try {
        const auto booking = m_reservations.findById(reservationId);
        if (!booking)
            return R::err(QStringLiteral("That booking no longer exists."));
        if (booking->status() != models::ReservationStatus::Seated)
            return R::err(QStringLiteral("Only a seated party can be closed off."));

        m_reservations.setStatus(reservationId, models::ReservationStatus::Completed);

        m_audit.log(QStringLiteral("RESV_DONE"), reservationTag(reservationId));
        m_notify.announceDataChanged(QStringLiteral("reservations"));
        return R::ok();
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

std::vector<models::Reservation> ReservationService::onDay(QDate day) const {
    if (!day.isValid())
        return {};
    try {
        return m_reservations.onDay(day);
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("bookings for %1 could not be read: %2")
                .arg(day.toString(Qt::ISODate), exceptionText(ex)));
        return {};
    }
}

std::vector<models::Table> ReservationService::tables() const {
    try {
        return m_tables.findAll();
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("tables could not be read: %1").arg(exceptionText(ex)));
        return {};
    }
}

} // namespace aluchop::services

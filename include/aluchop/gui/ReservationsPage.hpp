#pragma once

/**
 * @file ReservationsPage.hpp
 * @brief Screen 7 — table bookings: day picker, availability, seating and cancellation.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QString>

#include "aluchop/gui/Page.hpp"

class QComboBox;
class QDateEdit;
class QTableWidget;

namespace aluchop::gui {

/**
 * @brief The booking sheet for one day at a time.
 *
 * services::ReservationService answers the only hard question — which tables are free for a
 * given time window and party size — by checking overlaps in the persistence layer. The table
 * picker on this screen is populated from that answer, so a double booking cannot be created
 * through the UI, and the service re-validates anyway.
 *
 * Status actions map to the reservation lifecycle: Booked → Seated → Completed, with
 * Cancelled and No-show as terminal states.
 *
 * @oop-concept Enumerations (surfaced) :: the status column and its actions are
 *              models::ReservationStatus, not free-form strings
 */
class ReservationsPage final : public Page {
    Q_OBJECT
public:
    /// @param ctx composition root; uses AppContext::reservations() and customers().
    /// @param parent Qt parent.
    explicit ReservationsPage(services::AppContext& ctx, QWidget* parent = nullptr);

    /// @return "Reservations".
    QString pageTitle() const override;

    /// Reloads the bookings for the selected day and refreshes table availability.
    void refresh() override;

private slots:
    /// The day picker moved — reload the sheet for the new date.
    void onDayChanged();

    /// Creates a booking: customer, time window, guests, table, special requests.
    void onBook();

    /// Edits the selected booking, re-checking availability for the new window.
    void onEdit();

    /// Cancels the selected booking (with confirmation).
    void onCancel();

    /// Marks the selected booking as seated — the party has arrived.
    void onSeat();

    /// Marks the selected booking as completed — the table is free again.
    void onComplete();

private:
    /// @return the id of the selected reservation, or 0 when nothing is selected.
    int selectedReservationId() const;

    QDateEdit* m_day = nullptr;         ///< Day being viewed; defaults to today.
    QTableWidget* m_list = nullptr;     ///< Time | Customer | Guests | Table | Status | Requests.
    QComboBox* m_tablePicker = nullptr; ///< Only tables free for the chosen window and party size.
};

} // namespace aluchop::gui

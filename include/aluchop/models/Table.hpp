#pragma once

/// \file
/// \brief Table — a physical table on the restaurant floor.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QString>

namespace aluchop::models {

/// \brief A named table with a seating capacity.
///
/// Tables are the scarce resource the reservation system allocates:
/// ReservationService::availableTables() starts from every active table whose
/// capacity covers the party size, then removes the ones already blocked by an
/// overlapping booking.
class Table {
public:
    /// \brief Default constructor — used by TableRepository before hydration.
    Table() = default;

    /// @oop-concept Parameterised Constructor
    /// \param id       Database primary key, 0 when not yet persisted.
    /// \param name     Floor label, e.g. "T-04"; unique and non-blank.
    /// \param capacity Seats; must be at least 1.
    /// \throws core::ValidationException when a field fails its setter rule.
    Table(int id, QString name, int capacity);

    /// \brief Database primary key.
    int id() const noexcept { return m_id; }
    /// \brief Assign the primary key after an INSERT.
    void setId(int v) { m_id = v; }

    /// \brief Floor label shown on the reservation grid.
    const QString& name() const noexcept { return m_name; }
    /// \brief Set the floor label.
    /// \throws core::ValidationException when \p v trims to empty.
    void setName(const QString& v);

    /// \brief Number of seats.
    int capacity() const noexcept { return m_capacity; }
    /// \brief Set the number of seats.
    /// \throws core::ValidationException when \p c is below 1.
    void setCapacity(int c);

    /// \brief Whether the table is currently in service.
    bool isActive() const noexcept { return m_active; }
    /// \brief Take the table in or out of service (soft delete — past orders keep their table).
    void setActive(bool a) { m_active = a; }

private:
    int m_id = 0;         ///< Primary key.
    int m_capacity = 2;   ///< Seats; the commonest table is a two-top.
    QString m_name;       ///< Unique floor label.
    bool m_active = true; ///< Soft-delete flag.
};

} // namespace aluchop::models

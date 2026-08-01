#pragma once

/// \file
/// \brief Customer — the loyalty branch of the people diamond.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QDateTime>
#include <QString>

#include "aluchop/models/Person.hpp"

namespace aluchop::models {

/// @oop-concept Virtual Base Class :: the second edge of the diamond — Customer also
/// derives Person virtually, so a StaffCustomer has one identity, not two
/// \brief A guest in the loyalty database: contact details, points and visit count.
///
/// The class is deliberately thin. Order history is *not* a member: it lives in
/// the `orders` table and is fetched on demand by CustomerService, because a
/// customer object is loaded into list views hundreds at a time and must stay
/// cheap to copy.
class Customer : virtual public Person {
public:
    /// \brief Default constructor — used by CustomerRepository before hydration.
    Customer() = default;

    /// @oop-concept Parameterised Constructor :: enrol a guest in one statement
    /// \param id    Database primary key, 0 when not yet persisted.
    /// \param name  Display name; must not be blank.
    /// \param phone Contact number — the natural key used for lookup at the till.
    /// \param email Contact e-mail.
    /// \throws core::ValidationException when a field fails its setter rule.
    Customer(int id, QString name, QString phone, QString email);

    /// @oop-concept Method Overriding :: this branch of the hierarchy names itself
    /// \brief `"Customer"`.
    QString roleName() const override;

    /// \brief Current loyalty balance in points.
    int loyaltyPoints() const noexcept { return m_loyaltyPoints; }

    /// \brief Award points (one point per NPR 100 spent, awarded by BillingService).
    /// \throws core::ValidationException when \p pts is negative.
    void addLoyaltyPoints(int pts);

    /// \brief Spend points.
    /// \throws core::ValidationException when \p pts is negative or exceeds the
    ///         balance — the `CHECK (loyalty_points >= 0)` column constraint is
    ///         mirrored here so the error surfaces in the UI, not in the driver.
    void redeemPoints(int pts);

    /// \brief How many times this guest has been served.
    int visits() const noexcept { return m_visits; }

    /// \brief When the guest was first enrolled.
    QDateTime createdAt() const noexcept { return m_created; }

    /// \brief Set the enrolment timestamp (repository hydration only).
    void setCreatedAt(QDateTime t) { m_created = t; }

    /// \brief Overwrite the points balance (repository hydration only — use
    ///        addLoyaltyPoints()/redeemPoints() in business code).
    void setLoyaltyPoints(int p) { m_loyaltyPoints = p; }

    /// \brief Overwrite the visit count (repository hydration only — use
    ///        `++customer` in business code).
    void setVisits(int v) { m_visits = v; }

    /// @oop-concept Increment Operator :: "one more visit" is the domain's natural ++ —
    /// prefix and postfix, used by CustomerService::recordVisit
    /// \brief Prefix increment: record one more visit.
    /// \return `*this`, so the caller can chain or read the new count immediately.
    Customer& operator++();

    /// \brief Postfix increment: record one more visit.
    /// \return A copy taken **before** the increment, matching the built-in
    ///         semantics exactly; the unused `int` parameter is the standard tag
    ///         that distinguishes this overload from the prefix form.
    Customer operator++(int);

private:
    int m_loyaltyPoints = 0; ///< Redeemable points balance.
    int m_visits = 0;        ///< Lifetime visit count.
    QDateTime m_created;     ///< Enrolment timestamp (UTC).
};

} // namespace aluchop::models

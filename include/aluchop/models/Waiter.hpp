#pragma once

/// \file
/// \brief Waiter — floor staff whose pay includes tips.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/models/Employee.hpp"

namespace aluchop::models {

/// @oop-concept Hierarchical Inheritance :: Waiter/Chef/Manager all specialise Employee
/// \brief An employee who takes orders and serves tables.
///
/// \note Person is a virtual base of Employee, so Waiter's constructor must name
///       `Person(id, name, phone, email)` in its own member-initialiser list —
///       the `Person(...)` call inside Employee's constructor is skipped when
///       Waiter is the most-derived type.
class Waiter : public Employee {
public:
    /// \brief Default constructor — used by the repository factory before hydration.
    Waiter() = default;

    /// @oop-concept Parameterised Constructor :: position is implied by the type, not passed in
    /// \param id            Database primary key.
    /// \param name          Display name.
    /// \param phone         Contact number.
    /// \param email         Contact e-mail.
    /// \param monthlySalary Base salary in NPR.
    /// \param shift         Roster shift.
    /// \throws core::ValidationException when a field fails its setter rule.
    Waiter(int id, QString name, QString phone, QString email,
           core::Money monthlySalary, QString shift);

    /// \brief `"Waiter"`.
    QString roleName() const override;

    /// @oop-concept Method Overriding :: waiter pay = salary + tips
    /// \return `salary() + tipsThisMonth()`.
    core::Money monthlyPay() const override;

    /// \brief Tips collected so far this month.
    core::Money tipsThisMonth() const noexcept { return m_tips; }

    /// \brief Record a tip.
    /// \throws core::ValidationException when \p t is negative.
    void addTip(core::Money t);

    /// \brief Tables served this month — the headline performance figure.
    int tablesServed() const noexcept { return m_tablesServed; }

    /// \brief Set the tables-served counter.
    void setTablesServed(int n) { m_tablesServed = n; }

private:
    core::Money m_tips;      ///< Tips accumulated this month, in paisa.
    int m_tablesServed = 0;  ///< Tables served this month.
};

} // namespace aluchop::models

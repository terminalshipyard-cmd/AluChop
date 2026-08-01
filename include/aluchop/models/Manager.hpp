#pragma once

/// \file
/// \brief Manager — supervisory staff whose pay includes a bonus; also the
///        intermediate level of the Person → Employee → Manager → Admin chain.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/models/Employee.hpp"

namespace aluchop::models {

/// @oop-concept Single Inheritance :: Manager extends exactly one concrete base
/// \brief An employee who supervises the floor and earns a monthly bonus.
///
/// Its members are `protected` rather than `private` precisely because Admin
/// derives from it and legitimately reasons about the bonus.
///
/// \note Person is a virtual base of Employee, so Manager's constructor must name
///       `Person(id, name, phone, email)` in its own member-initialiser list.
class Manager : public Employee {
public:
    /// \brief Default constructor — used by the repository factory before hydration.
    Manager() = default;

    /// @oop-concept Default Arguments :: a manager may be hired before the bonus is agreed
    /// \param id            Database primary key.
    /// \param name          Display name.
    /// \param phone         Contact number.
    /// \param email         Contact e-mail.
    /// \param monthlySalary Base salary in NPR.
    /// \param shift         Roster shift.
    /// \param monthlyBonus  Fixed monthly bonus; defaults to nothing.
    /// \throws core::ValidationException when a field fails its setter rule.
    Manager(int id, QString name, QString phone, QString email,
            core::Money monthlySalary, QString shift,
            core::Money monthlyBonus = core::Money());

    /// \brief `"Manager"`.
    QString roleName() const override;

    /// @oop-concept Method Overriding :: manager pay = salary + bonus
    /// \return `salary() + monthlyBonus()`.
    core::Money monthlyPay() const override;

    /// \brief The fixed monthly bonus.
    core::Money monthlyBonus() const noexcept { return m_bonus; }

    /// \brief Set the fixed monthly bonus.
    /// \throws core::ValidationException when \p b is negative.
    void setMonthlyBonus(core::Money b);

protected:
    /// @oop-concept Protected Access :: Admin inherits and reasons about the bonus,
    /// while code outside the hierarchy still goes through the validating setter
    core::Money m_bonus; ///< Fixed monthly bonus, in paisa.
};

} // namespace aluchop::models

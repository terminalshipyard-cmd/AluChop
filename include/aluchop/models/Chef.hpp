#pragma once

/// \file
/// \brief Chef — kitchen staff whose pay includes overtime.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/models/Employee.hpp"

namespace aluchop::models {

/// @oop-concept Hierarchical Inheritance :: a second, independent specialisation of Employee
/// \brief An employee who cooks; sibling of Waiter under the same base.
///
/// \note Person is a virtual base of Employee, so Chef's constructor must name
///       `Person(id, name, phone, email)` in its own member-initialiser list.
class Chef : public Employee {
public:
    /// \brief Default constructor — used by the repository factory before hydration.
    Chef() = default;

    /// @oop-concept Default Arguments :: most chefs have no recorded specialty
    /// \param id            Database primary key.
    /// \param name          Display name.
    /// \param phone         Contact number.
    /// \param email         Contact e-mail.
    /// \param monthlySalary Base salary in NPR.
    /// \param shift         Roster shift.
    /// \param specialty     Cuisine speciality, e.g. "Tandoor"; optional.
    /// \throws core::ValidationException when a field fails its setter rule.
    Chef(int id, QString name, QString phone, QString email,
         core::Money monthlySalary, QString shift, QString specialty = QString());

    /// \brief `"Chef"`.
    QString roleName() const override;

    /// @oop-concept Method Overriding :: chef pay = salary + overtime
    /// \return `salary() + kOvertimeRatePerHour * overtimeHours()`.
    core::Money monthlyPay() const override;

    /// \brief Cuisine speciality; may be empty.
    const QString& specialty() const noexcept { return m_specialty; }

    /// \brief Set the cuisine speciality.
    void setSpecialty(const QString& s) { m_specialty = s; }

    /// \brief Overtime hours worked this month.
    int overtimeHours() const noexcept { return m_overtimeHours; }

    /// \brief Set the overtime hours.
    /// \throws core::ValidationException when \p h is negative.
    void setOvertimeHours(int h);

    /// @oop-concept Constant Objects / Static Members :: one immutable house overtime rate
    /// shared by every Chef object, defined once in Chef.cpp — a domain constant,
    /// not a magic number sprinkled through the payroll code
    /// \brief NPR 300.00 per overtime hour.
    static const core::Money kOvertimeRatePerHour;

private:
    QString m_specialty;     ///< Cuisine speciality; may be empty.
    int m_overtimeHours = 0; ///< Overtime hours this month.
};

} // namespace aluchop::models

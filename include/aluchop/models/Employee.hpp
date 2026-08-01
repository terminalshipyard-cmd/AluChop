#pragma once

/// \file
/// \brief Employee — the payroll-bearing branch of the people diamond.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QDate>
#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/models/Person.hpp"

namespace aluchop::models {

/// @oop-concept Virtual Base Class :: Employee virtually derives Person so StaffCustomer holds ONE identity
/// \brief Somebody on the payroll: a position, a salary, a shift and a rating.
///
/// Employee is concrete (it overrides Person::roleName()) because the repository
/// hydrates a *base-sliced* Employee for list views and edit forms, where the
/// specific role only matters as a string. When behaviour matters —
/// EmployeeService::payrollPreview() — EmployeeRepository::makeTyped() builds the
/// real Waiter / Chef / Manager / Admin on the heap instead and monthlyPay()
/// dispatches virtually.
///
/// The `virtual public Person` edge is not decoration. A waiter who joins the
/// loyalty programme is modelled by StaffCustomer, which inherits Employee *and*
/// Customer. Without virtual inheritance that object would own two ids, two names
/// and two phone numbers, and `staff.name()` would not even compile.
///
/// \warning Because Person is a virtual base, **the most-derived class**
///          initialises it. Any class deriving from Employee must therefore name
///          `Person(id, name, phone, email)` in its own member-initialiser list;
///          the `Person(...)` call written inside Employee's constructor is
///          ignored for those objects and the identity would silently come out
///          blank. See Waiter.hpp / Chef.hpp / Manager.hpp / Admin.hpp.
class Employee : virtual public Person {
public:
    /// \brief Default constructor — used by EmployeeRepository before hydration.
    Employee() = default;

    /// @oop-concept Parameterised Constructor :: hire an employee in one statement
    /// \param id            Database primary key, 0 when not yet persisted.
    /// \param name          Display name; must not be blank.
    /// \param phone         Contact number.
    /// \param email         Contact e-mail.
    /// \param position      One of "WAITER", "CHEF", "MANAGER", "ADMIN".
    /// \param monthlySalary Base monthly salary in NPR; must not be negative.
    /// \param shift         Roster shift, e.g. "DAY" or "NIGHT".
    /// \throws core::ValidationException when any field fails its setter rule.
    Employee(int id, QString name, QString phone, QString email,
             QString position, core::Money monthlySalary, QString shift);

    /// @oop-concept Method Overriding :: the abstract role name becomes concrete here
    /// \brief `"Employee"` — refined by every subclass.
    QString roleName() const override;

    /// @oop-concept Virtual Functions :: payroll is computed polymorphically per role
    /// \brief Total pay for the current month.
    /// \return The base salary. Waiter adds tips, Chef adds overtime, Manager
    ///         adds a bonus — each by overriding this one function, which is why
    ///         the payroll loop needs no `if (role == …)` chain anywhere.
    virtual core::Money monthlyPay() const;

    /// \brief Position token as stored in the `employees.position` column.
    const QString& position() const noexcept { return m_position; }

    /// \brief Set the position token.
    /// \throws core::ValidationException when \p v is not one of the four
    ///         positions permitted by the schema's CHECK constraint.
    void setPosition(const QString& v);

    /// @oop-concept Constant Member Functions :: observers never mutate, so const objects stay useful
    /// \brief Base monthly salary (never a double — integer paisa).
    core::Money salary() const noexcept { return m_salary; }

    /// \brief Set the base monthly salary.
    /// \throws core::ValidationException when \p v is negative.
    void setSalary(core::Money v);

    /// \brief Roster shift label.
    const QString& shift() const noexcept { return m_shift; }

    /// \brief Set the roster shift.
    /// \throws core::ValidationException when \p v trims to empty.
    void setShift(const QString& v);

    /// \brief Date of joining; invalid QDate when unknown.
    QDate hiredDate() const noexcept { return m_hired; }

    /// \brief Set the date of joining.
    void setHiredDate(QDate d) { m_hired = d; }

    /// \brief Whether the employee still works here.
    bool isActive() const noexcept { return m_active; }

    /// \brief Activate or deactivate (soft delete — payroll history is kept).
    void setActive(bool a) { m_active = a; }

    /// \brief Performance rating, 1 (poor) to 5 (excellent).
    int performanceRating() const noexcept { return m_rating; }

    /// \brief Set the performance rating.
    /// \param r Raw rating; silently clamped into 1..5 via core::clampValue so a
    ///          slider or spin box can never push an out-of-range value into a
    ///          column guarded by `CHECK (performance_rating BETWEEN 1 AND 5)`.
    void setPerformanceRating(int r);

protected:
    QString m_position;                          ///< "WAITER" | "CHEF" | "MANAGER" | "ADMIN".
    core::Money m_salary;                        ///< Base monthly salary in paisa.
    QString m_shift = QStringLiteral("DAY");     ///< Roster shift; defaults to the day shift.
    QDate m_hired;                               ///< Date of joining.
    bool m_active = true;                        ///< Soft-delete flag.
    int m_rating = 3;                            ///< Performance rating, 1..5.
};

} // namespace aluchop::models

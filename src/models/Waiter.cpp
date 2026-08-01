/// \file
/// \brief Implementation of Waiter — floor staff paid a salary plus tips.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Waiter.hpp"

#include <utility>

#include <QString>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Money.hpp"

namespace aluchop::models {

/// @oop-concept Parameterised Constructor :: the position token is implied by the type
Waiter::Waiter(int id, QString name, QString phone, QString email,
               core::Money monthlySalary, QString shift)
    // Person is a virtual base of Employee, so Waiter — the most-derived class —
    // must construct it itself; the Person(...) call inside Employee's own
    // constructor is skipped for this object.
    : Person(id, name, phone, email),
      Employee(id, name, phone, email, QStringLiteral("WAITER"), monthlySalary, std::move(shift))
{
    // Nothing further: tips and tables served start at zero and accumulate.
}

QString Waiter::roleName() const
{
    return QStringLiteral("Waiter");
}

/// @oop-concept Method Overriding :: waiter pay = base salary + tips earned this month
core::Money Waiter::monthlyPay() const
{
    // A genuinely different rule from Employee::monthlyPay(), not a cosmetic
    // one: a waiter on the same base salary as a chef takes home a different
    // amount, and the payroll loop discovers that through this override alone.
    return salary() + m_tips;
}

void Waiter::addTip(core::Money t)
{
    if (t.isNegative())
        throw core::ValidationException("a tip cannot be negative");
    m_tips += t; // Money::operator+= — integer paisa, no rounding drift
}

} // namespace aluchop::models

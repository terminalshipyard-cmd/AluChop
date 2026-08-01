/// \file
/// \brief Implementation of Manager — supervisory staff paid a salary plus a
///        fixed bonus, and the middle link of the multilevel chain
///        Person → Employee → Manager → Admin.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Manager.hpp"

#include <utility>

#include <QString>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Money.hpp"

namespace aluchop::models {

/// @oop-concept Default Arguments :: a manager may be hired before the bonus is agreed
Manager::Manager(int id, QString name, QString phone, QString email,
                 core::Money monthlySalary, QString shift, core::Money monthlyBonus)
    // Person is a virtual base of Employee, so Manager names it directly. Admin
    // derives from Manager and does the same thing one level further down — the
    // rule applies at every level, always to the most-derived class.
    : Person(id, name, phone, email),
      Employee(id, name, phone, email, QStringLiteral("MANAGER"), monthlySalary, std::move(shift))
{
    setMonthlyBonus(monthlyBonus);
}

QString Manager::roleName() const
{
    return QStringLiteral("Manager");
}

/// @oop-concept Method Overriding :: manager pay = base salary + fixed monthly bonus
core::Money Manager::monthlyPay() const
{
    return salary() + m_bonus;
}

void Manager::setMonthlyBonus(core::Money b)
{
    if (b.isNegative())
        throw core::ValidationException("monthly bonus must not be negative");
    m_bonus = b;
}

} // namespace aluchop::models

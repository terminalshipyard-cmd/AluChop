/// \file
/// \brief Implementation of Employee — the payroll branch of the people diamond.
///
/// Two things in this file are load-bearing for the whole hierarchy:
///
///  1. `monthlyPay()` returns the *base* salary and nothing else. Every
///     specialised role overrides it and adds its own component (tips, overtime,
///     bonus). Because the base rule is honest rather than empty, the payroll
///     loop in EmployeeService can hold `Employee*` and never ask what it points
///     at, and a role that has no extra component simply does not override.
///
///  2. The `Person(...)` call in the constructor below is **ignored** whenever an
///     Employee is an intermediate base — which is every time a Waiter, Chef,
///     Manager, Admin or StaffCustomer is constructed. That is the defining rule
///     of virtual bases: the most-derived class initialises them. What still runs
///     for those objects is this constructor's *body*, and it operates on the one
///     shared Person subobject.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Employee.hpp"

#include <string>

#include <QString>
#include <QStringList>

#include "aluchop/core/Algorithms.hpp"
#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Money.hpp"

namespace aluchop::models {
namespace {

/// \brief Lowest legal performance rating (mirrors the schema CHECK).
constexpr int kMinRating = 1;

/// \brief Highest legal performance rating (mirrors the schema CHECK).
constexpr int kMaxRating = 5;

/// \brief The four position tokens permitted by `employees.position`.
/// \return The CHECK list, in schema order.
const QStringList& legalPositions()
{
    static const QStringList kPositions{
        QStringLiteral("WAITER"),
        QStringLiteral("CHEF"),
        QStringLiteral("MANAGER"),
        QStringLiteral("ADMIN"),
    };
    return kPositions;
}

} // namespace

/// @oop-concept Parameterised Constructor :: hire an employee in one statement
Employee::Employee(int id, QString name, QString phone, QString email,
                   QString position, core::Money monthlySalary, QString shift)
    : Person(id, std::move(name), std::move(phone), std::move(email))
{
    setPosition(position);
    setSalary(monthlySalary);
    setShift(shift);
}

/// @oop-concept Method Overriding :: the pure virtual role name becomes concrete here
QString Employee::roleName() const
{
    return QStringLiteral("Employee");
}

/// @oop-concept Virtual Functions :: the base payroll rule, refined by every role
core::Money Employee::monthlyPay() const
{
    return m_salary;
}

void Employee::setPosition(const QString& v)
{
    const QString token = v.trimmed().toUpper();
    if (!legalPositions().contains(token))
        throw core::ValidationException("position must be one of WAITER, CHEF, MANAGER, ADMIN — got '"
                                        + v.toStdString() + "'");
    m_position = token;
}

void Employee::setSalary(core::Money v)
{
    if (v.isNegative())
        throw core::ValidationException("salary must not be negative");
    m_salary = v;
}

void Employee::setShift(const QString& v)
{
    const QString trimmed = v.trimmed();
    if (trimmed.isEmpty())
        throw core::ValidationException("shift must not be blank");
    // Stored as typed (only trimmed): unlike `position`, the shift column carries
    // no CHECK constraint, so a rota label the manager invents must survive a
    // save/load round trip byte for byte.
    m_shift = trimmed;
}

void Employee::setPerformanceRating(int r)
{
    // Clamped rather than rejected: a slider or spin box must not be able to
    // throw at the user, but the column CHECK (1..5) must still hold.
    /// @oop-concept Function Template :: core::clampValue is instantiated for int here
    m_rating = core::clampValue(r, kMinRating, kMaxRating);
}

} // namespace aluchop::models

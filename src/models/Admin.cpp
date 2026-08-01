/// \file
/// \brief Implementation of Admin — the deepest level of the staff chain and the
///        only role permitted to manage login accounts.
///
/// Admin is where three inheritance forms meet on one class:
///  * **multilevel** — Person → Employee → Manager → Admin, each level adding
///    real state (identity → payroll → bonus → privilege);
///  * **multiple** — it also derives the pure-abstract IAuditable mixin;
///  * **hybrid** — that multiple inheritance sits on top of a chain rooted in a
///    virtual base.
///
/// It deliberately does *not* override monthlyPay(): an admin is paid exactly as
/// a manager is, and inheriting the rule rather than copying it is the honest
/// modelling choice.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Admin.hpp"

#include <utility>

#include <QString>

#include "aluchop/core/Money.hpp"

namespace aluchop::models {

/// @oop-concept Multilevel Inheritance :: the virtual base is still initialised here,
/// three levels below Person, because Admin is the most-derived class
Admin::Admin(int id, QString name, QString phone, QString email,
             core::Money monthlySalary, QString shift)
    : Person(id, name, phone, email),
      Manager(id, name, phone, email, monthlySalary, std::move(shift))
{
    // Manager's constructor body stamped the position as "MANAGER"; an admin's
    // employees.position column must read "ADMIN", so it is corrected here —
    // after the base is fully constructed, which is the only safe moment.
    setPosition(QStringLiteral("ADMIN"));
}

/// @oop-concept Final Override :: the privilege chain stops here, so no future subclass
/// can inherit administrative rights and then report a weaker-sounding role name
QString Admin::roleName() const
{
    return QStringLiteral("Admin");
}

/// @oop-concept Multiple Inheritance :: this is the IAuditable half of the class —
/// a capability that has nothing to do with being a Manager, which is exactly why
/// it arrives through a separate, stateless base
QString Admin::auditDescription() const
{
    return QStringLiteral("Admin %1 (id %2)").arg(name()).arg(id());
}

} // namespace aluchop::models

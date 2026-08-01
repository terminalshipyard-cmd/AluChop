/// \file
/// \brief Implementation of StaffCustomer — the diamond, genuinely solved.
///
/// A waiter who is enrolled in the loyalty programme is one human being with one
/// name, one phone number and one id, who happens to appear in two tables. This
/// class is that human being.
///
/// \par Why the constructor looks like this
/// Person is a **virtual base** of both Employee and Customer, and a virtual base
/// is initialised by the *most-derived* class only. StaffCustomer is that class,
/// so its member-initialiser list names `Person(...)` directly. The `Person(...)`
/// calls written inside Employee's and Customer's own constructors are skipped
/// entirely for this object — if StaffCustomer did not name Person itself, the
/// identity would be default-constructed and every field would come out blank.
/// The bases are listed in the order the language will run them anyway (virtual
/// base first, then Employee, then Customer) so that reading the code matches
/// what actually happens.
///
/// \par What virtual inheritance buys, concretely
/// There is exactly **one** Person subobject. `setId(7)` updates the id seen by
/// both branches; `name()` is unambiguous and compiles; and the employee half can
/// never drift out of sync with the customer half, because there is no second
/// half to drift from. Remove `virtual` from the two edges and this file stops
/// compiling — `name()` becomes "found in multiple base-class subobjects".
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/StaffCustomer.hpp"

#include <QString>

#include "aluchop/core/Money.hpp"

namespace aluchop::models {

/// @oop-concept Virtual Base Class :: the most-derived class constructs the shared Person
StaffCustomer::StaffCustomer(int personId, QString name, QString phone, QString email,
                             QString position, core::Money monthlySalary, QString shift)
    : Person(personId, name, phone, email),
      Employee(personId, name, phone, email, std::move(position), monthlySalary, std::move(shift)),
      Customer(personId, name, phone, email)
{
    // The identity arguments are passed (not moved) into all three initialisers
    // because Employee and Customer still need well-formed values for their own
    // bodies; QString is implicitly shared, so the copies cost a refcount each.
    //
    // Only the Person(...) initialiser above actually constructs the identity.
    // The Employee and Customer initialisers still run their *bodies*, which set
    // the position, salary, shift and enrolment timestamp on this one object.
}

/// @oop-concept Method Overriding :: resolves the diamond's ambiguous final overrider
QString StaffCustomer::roleName() const
{
    // Employee::roleName() and Customer::roleName() are both final overriders of
    // Person::roleName() along different paths, which makes the inherited
    // overrider ambiguous. Declaring one here is what makes the class
    // instantiable at all — it is a language requirement, not a stylistic choice.
    return QStringLiteral("Staff Member");
}

QString StaffCustomer::displayLabel() const
{
    // Reaches into both branches in a single expression: name() comes from the
    // shared Person, loyaltyPoints() from the Customer branch. That this is even
    // well-formed is the whole point of the virtual base.
    return QStringLiteral("%1 (Staff · %2 pts)").arg(name()).arg(loyaltyPoints());
}

} // namespace aluchop::models

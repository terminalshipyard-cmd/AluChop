#pragma once

/// \file
/// \brief StaffCustomer — the diamond: a member of staff who is also enrolled in
///        the loyalty programme.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/models/Customer.hpp"
#include "aluchop/models/Employee.hpp"

namespace aluchop::models {

/// @oop-concept Hybrid Inheritance / Virtual Base Class :: a staff member who is also a loyalty
/// customer — without `virtual public Person` this object would carry two names and two ids.
/// \brief A person who is simultaneously on the payroll and in the loyalty database.
///
/// This is a real situation in a restaurant: staff eat here, they get a staff
/// discount, and management still wants their spend on the loyalty ledger.
/// EmployeeService::staffCustomerFor() fuses the two rows when a customer's phone
/// number matches an active employee, and BillingService then offers the staff
/// discount as one of the competing discount candidates.
///
/// The inheritance graph is:
/// \verbatim
///                Person            (virtual base — ONE id, ONE name, ONE phone)
///               /      \
///     virtual  /        \  virtual
///        Employee      Customer
///               \      /
///             StaffCustomer
/// \endverbatim
///
/// Had Employee and Customer derived Person non-virtually, a StaffCustomer would
/// contain two Person subobjects: `sc.name()` would be ambiguous and refuse to
/// compile, `sc.setId(7)` would update only one half, and the two halves could
/// drift apart. The `virtual` keyword on both edges collapses them into a single
/// shared identity — the diamond genuinely solved, not demonstrated.
///
/// \par Constructor rule (binding)
/// A virtual base is initialised by the **most-derived** class only. StaffCustomer's
/// member-initialiser list must therefore read
/// `Person(personId, name, phone, email), Employee(...), Customer(...)`; the
/// `Person(...)` calls written inside the Employee and Customer constructors are
/// ignored when those classes are intermediate bases.
class StaffCustomer : public Employee, public Customer {
public:
    /// \brief Default constructor — used before hydration from two rows.
    StaffCustomer() = default;

    /// @oop-concept Parameterised Constructor :: one identity feeds both branches
    /// \param personId      The single shared primary key of the fused identity.
    /// \param name          Display name.
    /// \param phone         Contact number — the key that matched the two rows.
    /// \param email         Contact e-mail.
    /// \param position      Employment position token.
    /// \param monthlySalary Base salary in NPR.
    /// \param shift         Roster shift.
    /// \throws core::ValidationException when a field fails its setter rule.
    StaffCustomer(int personId, QString name, QString phone, QString email,
                  QString position, core::Money monthlySalary, QString shift);

    /// @oop-concept Method Overriding :: resolves the diamond's ambiguous final overrider
    /// \brief `"Staff Member"`.
    ///
    /// Employee::roleName() and Customer::roleName() are both final overriders of
    /// Person::roleName() along different paths. Overriding it here is not
    /// optional — it is what makes the class instantiable at all.
    QString roleName() const override;

    /// \brief `"<name> (Staff · <loyaltyPoints> pts)"`.
    ///
    /// Reaches into both branches at once — `name()` from the shared Person and
    /// `loyaltyPoints()` from the Customer branch — which is only meaningful
    /// because there is exactly one name to reach for.
    QString displayLabel() const override;

    /// @oop-concept Inline Functions :: a constant house rule, queried on every staff bill
    /// \brief Staff eat at 10% off; competes with promo codes, never stacks with them.
    int staffDiscountPercent() const noexcept { return 10; }
};

} // namespace aluchop::models

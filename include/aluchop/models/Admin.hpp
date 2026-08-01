#pragma once

/// \file
/// \brief Admin — the deepest level of the staff chain and the only role that
///        can manage user accounts.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/models/Interfaces.hpp"
#include "aluchop/models/Manager.hpp"

namespace aluchop::models {

/// @oop-concept Multilevel Inheritance :: Person → Employee → Manager → Admin,
/// each level adding real state (identity → payroll → bonus → privilege)
/// @oop-concept Multiple Inheritance :: a concrete role base plus a capability interface
/// @oop-concept Hybrid Inheritance :: multilevel over a virtual base, combined with an interface
/// \brief A manager with system-administration privileges.
///
/// Admin implements IAuditable because privileged actions — creating users,
/// restoring a backup, voiding a paid order — must land in the audit trail
/// naming the human responsible. That capability is orthogonal to being a
/// manager, which is exactly why it arrives through a separate base rather than
/// as another virtual function bolted onto Employee.
///
/// \note Person is a virtual base, so Admin's constructor must name
///       `Person(id, name, phone, email)` in its own member-initialiser list.
class Admin : public Manager, public IAuditable {
public:
    /// \brief Default constructor — used by the repository factory before hydration.
    Admin() = default;

    /// @oop-concept Parameterised Constructor
    /// \param id            Database primary key.
    /// \param name          Display name.
    /// \param phone         Contact number.
    /// \param email         Contact e-mail.
    /// \param monthlySalary Base salary in NPR.
    /// \param shift         Roster shift.
    /// \throws core::ValidationException when a field fails its setter rule.
    Admin(int id, QString name, QString phone, QString email,
          core::Money monthlySalary, QString shift);

    /// @oop-concept Final Override :: the privilege chain stops here — no role may
    /// inherit from Admin and then report a weaker-sounding name
    /// \brief `"Admin"`.
    QString roleName() const final;

    /// \brief One-line identity for the audit trail.
    /// \return `"Admin <name> (id <id>)"`.
    QString auditDescription() const override;

    /// @oop-concept Inline Functions :: a one-word authorisation check, called on
    /// every privileged screen, is defined in-class so it costs nothing
    /// \brief Admins, and only admins, may create and edit login accounts.
    bool canManageUsers() const noexcept { return true; }
};

} // namespace aluchop::models

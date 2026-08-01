#pragma once

/// \file
/// \brief Person — the abstract root of the people hierarchy and the **virtual
///        base** at the top of the Employee/Customer diamond.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QString>

namespace aluchop::models {

/// @oop-concept Abstract Classes :: nobody is "just a Person" in the restaurant — always a role
/// \brief Shared identity of every human the system knows about.
///
/// Person owns exactly the fields that a restaurant needs regardless of role:
/// a database id, a name, a phone number and an e-mail address. It deliberately
/// stops there — salary belongs to Employee, loyalty points belong to Customer.
///
/// It is abstract because roleName() is pure virtual: the application never has a
/// reason to instantiate a faceless person, and forcing every concrete type to
/// name its own role is what makes labels such as `"Ramesh (Waiter)"` work
/// through a single base pointer.
///
/// \note Person is inherited **virtually** by both Employee and Customer, so a
///       StaffCustomer carries exactly one id, one name, one phone and one
///       e-mail. See StaffCustomer.hpp for the consequences on constructors.
class Person {
public:
    /// \brief Default constructor — used by repositories before hydrating fields.
    Person() = default;

    /// @oop-concept Parameterised Constructor :: build a fully-formed identity in one step
    /// \brief Construct a validated identity.
    /// \param id    Database primary key, 0 for an object not yet persisted.
    /// \param name  Display name; must not be blank.
    /// \param phone Contact number; may be empty, otherwise 7–15 digits.
    /// \param email Contact e-mail; may be empty, otherwise must contain '@'.
    /// \throws core::ValidationException when a field fails the setter rules.
    Person(int id, QString name, QString phone, QString email);

    /// @oop-concept Virtual Destructor :: people are always held as `Person*` /
    /// `std::unique_ptr<Employee>`, so destruction must dispatch to the real type
    virtual ~Person() = default;

    /// @oop-concept Pure Virtual Function :: every concrete role must name itself
    /// \brief The role this person plays, e.g. `"Waiter"`.
    virtual QString roleName() const = 0;

    /// @oop-concept Virtual Functions :: a sensible default that specialised roles refine
    /// \brief One-line label for lists and combo boxes.
    /// \return By default `"<name> (<roleName()>)"`; StaffCustomer overrides it to
    ///         also surface loyalty points.
    virtual QString displayLabel() const;

    /// \brief Database primary key; 0 means "not persisted yet".
    int id() const noexcept { return m_id; }

    /// \brief Assign the primary key after an INSERT.
    void setId(int id) { m_id = id; }

    /// @oop-concept Return by Reference :: identity fields exposed as const refs, no copies
    /// \brief Display name (never blank on a validated object).
    const QString& name() const noexcept { return m_name; }

    /// \brief Contact number; possibly empty.
    const QString& phone() const noexcept { return m_phone; }

    /// \brief Contact e-mail; possibly empty.
    const QString& email() const noexcept { return m_email; }

    /// @oop-concept Pass by Reference :: strings arrive as `const&`, never copied at the call
    /// \brief Set the display name.
    /// \throws core::ValidationException when \p v trims to an empty string.
    void setName(const QString& v);

    /// \brief Set the contact number.
    /// \throws core::ValidationException when \p v is non-empty but is not 7–15 digits.
    void setPhone(const QString& v);

    /// \brief Set the contact e-mail.
    /// \throws core::ValidationException when \p v is non-empty but contains no '@'.
    void setEmail(const QString& v);

protected:
    int m_id = 0;      ///< Primary key; shared by every branch of the diamond.
    QString m_name;    ///< Display name.
    QString m_phone;   ///< Contact number, possibly empty.
    QString m_email;   ///< Contact e-mail, possibly empty.
};

} // namespace aluchop::models

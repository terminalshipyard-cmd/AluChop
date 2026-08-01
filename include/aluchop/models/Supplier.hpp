#pragma once

/// \file
/// \brief Supplier — a vendor the restaurant restocks from.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QString>

namespace aluchop::models {

/// \brief Contact details of a goods supplier.
///
/// Suppliers are referenced by Ingredient::supplierId() so the Inventory screen
/// can show a purchase officer who to ring when stock runs low.
///
/// Deliberately *not* a Person: a supplier is an organisation, has no role in the
/// restaurant, no salary and no loyalty balance. Forcing it into the people
/// hierarchy for the sake of reusing four fields would be inheritance for code
/// reuse rather than for an is-a relationship.
class Supplier {
public:
    /// \brief Default constructor — used by SupplierRepository before hydration.
    Supplier() = default;

    /// @oop-concept Parameterised Constructor
    /// \param id      Database primary key, 0 when not yet persisted.
    /// \param name    Company name; must not be blank.
    /// \param phone   Contact number.
    /// \param email   Contact e-mail.
    /// \param address Delivery/billing address.
    /// \throws core::ValidationException when \p name trims to empty.
    Supplier(int id, QString name, QString phone, QString email, QString address);

    /// \brief Database primary key.
    int id() const noexcept { return m_id; }
    /// \brief Assign the primary key after an INSERT.
    void setId(int v) { m_id = v; }

    /// \brief Company name.
    const QString& name() const noexcept { return m_name; }
    /// \brief Set the company name.
    /// \throws core::ValidationException when \p v trims to empty.
    void setName(const QString& v);

    /// \brief Contact number.
    const QString& phone() const noexcept { return m_phone; }
    /// \brief Set the contact number.
    void setPhone(const QString& v) { m_phone = v; }

    /// \brief Contact e-mail.
    const QString& email() const noexcept { return m_email; }
    /// \brief Set the contact e-mail.
    void setEmail(const QString& v) { m_email = v; }

    /// \brief Delivery/billing address.
    const QString& address() const noexcept { return m_address; }
    /// \brief Set the address.
    void setAddress(const QString& v) { m_address = v; }

private:
    int m_id = 0;       ///< Primary key.
    QString m_name;     ///< Company name.
    QString m_phone;    ///< Contact number.
    QString m_email;    ///< Contact e-mail.
    QString m_address;  ///< Delivery/billing address.
};

} // namespace aluchop::models

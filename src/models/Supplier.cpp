/// \file
/// \brief Implementation of models::Supplier.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Supplier.hpp"

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::models {

Supplier::Supplier(int id, QString name, QString phone, QString email, QString address)
    : m_id(id)
{
    setName(name);
    // Phone, e-mail and address carry no invariant: a supplier the purchase
    // officer only ever e-mails legitimately has no phone number on file.
    setPhone(phone.trimmed());
    setEmail(email.trimmed());
    setAddress(address.trimmed());
}

void Supplier::setName(const QString& v)
{
    const QString trimmed = v.trimmed();
    if (trimmed.isEmpty())
        throw core::ValidationException("Supplier name must not be blank");
    m_name = trimmed;
}

} // namespace aluchop::models

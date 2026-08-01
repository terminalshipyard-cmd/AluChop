/// \file
/// \brief Implementation of Customer — the loyalty branch of the people diamond.
///
/// Customer is the second `virtual public Person` edge. On its own it is an
/// ordinary small entity; the virtual keyword only starts paying for itself in
/// StaffCustomer, where it collapses the two inherited Person subobjects into
/// one shared identity.
///
/// The increment operators are the interesting part. "One more visit" is the
/// single most frequent mutation a customer record undergoes, and it reads far
/// better as `++customer` than as `customer.setVisits(customer.visits() + 1)`.
/// Both forms are provided with exactly the built-in semantics: prefix returns a
/// reference to the updated object, postfix returns a copy of the value before
/// the increment.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Customer.hpp"

#include <string>

#include <QDateTime>
#include <QString>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::models {

/// @oop-concept Parameterised Constructor :: enrol a guest in one statement
Customer::Customer(int id, QString name, QString phone, QString email)
    : Person(id, std::move(name), std::move(phone), std::move(email)),
      m_created(QDateTime::currentDateTimeUtc())
{
    // Enrolment time is stamped here so a freshly created guest is never left
    // with an invalid QDateTime; CustomerRepository overwrites it with the
    // stored value when hydrating an existing row.
}

/// @oop-concept Method Overriding :: this branch of the hierarchy names itself
QString Customer::roleName() const
{
    return QStringLiteral("Customer");
}

void Customer::addLoyaltyPoints(int pts)
{
    if (pts < 0)
        throw core::ValidationException("cannot award a negative number of loyalty points ("
                                        + std::to_string(pts) + ")");
    m_loyaltyPoints += pts;
}

void Customer::redeemPoints(int pts)
{
    if (pts < 0)
        throw core::ValidationException("cannot redeem a negative number of loyalty points ("
                                        + std::to_string(pts) + ")");
    if (pts > m_loyaltyPoints)
        throw core::ValidationException("loyalty balance is only " + std::to_string(m_loyaltyPoints)
                                        + " points, cannot redeem " + std::to_string(pts));
    m_loyaltyPoints -= pts;
}

/// @oop-concept Increment Operator (prefix) :: the domain's natural "one more visit"
Customer& Customer::operator++()
{
    ++m_visits;
    /// @oop-concept Return by Reference :: the caller sees the updated object, no copy made
    return *this;
}

/// @oop-concept Increment Operator (postfix) :: the unused int tag distinguishes the overload
Customer Customer::operator++(int)
{
    Customer before(*this); // implicit copy constructor — value taken before the change
    ++(*this);              // delegate to the prefix form: one increment rule, not two
    return before;
}

} // namespace aluchop::models

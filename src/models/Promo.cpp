/// \file
/// \brief Implementation of models::Promo.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Promo.hpp"

#include <string>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::models {

void Promo::setCode(const QString& v)
{
    const QString trimmed = v.trimmed();
    if (trimmed.isEmpty())
        throw core::ValidationException("Promo code must not be blank");
    // Stored upper-case so "newyear", "NewYear" and "NEWYEAR" are one promo and
    // the repository lookup can stay a plain equality test.
    m_code = trimmed.toUpper();
}

void Promo::setPercent(int p)
{
    if (p < 0 || p > 100)
        throw core::ValidationException(
            "Promo percentage must be between 0 and 100 (got " + std::to_string(p) + ")");
    m_percent = p;
}

void Promo::setFlatAmount(core::Money v)
{
    if (v.isNegative())
        throw core::ValidationException("Promo flat amount must not be negative");
    m_flat = v;
}

bool Promo::isValidOn(QDate day, core::Money orderSubtotal) const
{
    if (!m_active)
        return false;

    if (!day.isValid())
        return false;

    // An invalid bound is deliberately read as "open ended", which is how a promo
    // with no end date is expressed in the database (NULL valid_to).
    if (m_validFrom.isValid() && day < m_validFrom)
        return false;
    if (m_validTo.isValid() && day > m_validTo)
        return false;

    // The floor is compared against the tax-inclusive subtotal, because that is
    // the only figure the guest ever sees on the menu.
    return orderSubtotal >= m_minOrder;
}

core::Money Promo::discountFor(core::Money subtotal) const
{
    if (subtotal.isNegative() || subtotal.isZero())
        return core::Money::zero();

    switch (m_kind) {
    case PromoKind::Percent:
        return subtotal.percent(m_percent);

    case PromoKind::Flat:
        // A flat promo larger than the bill discounts the bill to zero and no
        // further — the till may never end up owing the guest money.
        return m_flat < subtotal ? m_flat : subtotal;
    }

    return core::Money::zero();
}

} // namespace aluchop::models

/// \file
/// \brief Implementation of models::Ingredient.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Ingredient.hpp"

#include <string>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::models {

Ingredient::Ingredient(int id, QString name, QString unit, double stockQty, double lowThreshold)
    : m_id(id)
{
    setName(name);
    setUnit(unit);
    setStockQty(stockQty);
    setLowThreshold(lowThreshold);
}

void Ingredient::setName(const QString& v)
{
    const QString trimmed = v.trimmed();
    if (trimmed.isEmpty())
        throw core::ValidationException("Ingredient name must not be blank");
    m_name = trimmed;
}

void Ingredient::setUnit(const QString& v)
{
    const QString trimmed = v.trimmed();
    if (trimmed.isEmpty())
        throw core::ValidationException(
            "Ingredient unit must not be blank (expected e.g. \"kg\", \"l\", \"pcs\")");
    m_unit = trimmed;
}

void Ingredient::setStockQty(double v)
{
    // Mirrors the schema's CHECK (stock_qty >= 0): an over-deduction is reported
    // here as a domain error rather than surfacing later as a driver error.
    if (v < 0.0)
        throw core::ValidationException(
            "Stock quantity must not be negative (got " + std::to_string(v) + ")");
    m_stockQty = v;
}

void Ingredient::setLowThreshold(double v)
{
    if (v < 0.0)
        throw core::ValidationException(
            "Low-stock threshold must not be negative (got " + std::to_string(v) + ")");
    m_lowThreshold = v;
}

void Ingredient::setUnitCost(core::Money c)
{
    if (c.isNegative())
        throw core::ValidationException("Ingredient unit cost must not be negative");
    m_unitCost = c;
}

bool Ingredient::expiresWithin(int days) const
{
    // No expiry recorded means "does not expire" — a bag of salt must never
    // appear on the expiring-goods alert list.
    if (!m_expiry.isValid())
        return false;

    const QDate today = QDate::currentDate();

    // Already expired counts as "expiring within any window": stock that went
    // off yesterday is more urgent than stock going off tomorrow, so it must not
    // silently drop out of the alert.
    if (m_expiry < today)
        return true;

    return today.daysTo(m_expiry) <= static_cast<qint64>(days);
}

} // namespace aluchop::models

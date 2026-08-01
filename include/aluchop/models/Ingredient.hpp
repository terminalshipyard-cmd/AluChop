#pragma once

/// \file
/// \brief Ingredient — one stock item in the store room.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QDate>
#include <QString>

#include "aluchop/core/Money.hpp"

namespace aluchop::models {

/// \brief A raw material with a quantity, a reorder threshold and an expiry date.
///
/// Stock is deducted automatically: when an order reaches status Served,
/// InventoryService walks each dish's recipe (RecipeLine) and subtracts
/// `qtyPerServing × qty` from the matching Ingredient.
///
/// \note Quantities here are `double` on purpose and this is **not** a violation
///       of the no-double-money rule. 0.75 kg of chicken is a physical
///       measurement whose precision is set by a kitchen scale; NPR 0.75 is
///       money and lives in core::Money as integer paisa. unitCost() is money and
///       is typed accordingly.
class Ingredient {
public:
    /// \brief Default constructor — used by IngredientRepository before hydration.
    Ingredient() = default;

    /// @oop-concept Parameterised Constructor :: a stock item needs a unit and a threshold to be useful
    /// \param id            Database primary key, 0 when not yet persisted.
    /// \param name          Ingredient name; must not be blank and is unique in the schema.
    /// \param unit          Unit of measure, e.g. "kg", "l", "pcs"; must not be blank.
    /// \param stockQty      Quantity currently in store; must not be negative.
    /// \param lowThreshold  Quantity at or below which a low-stock alert fires.
    /// \throws core::ValidationException when a field fails its setter rule.
    Ingredient(int id, QString name, QString unit, double stockQty, double lowThreshold);

    /// \brief Database primary key.
    int id() const noexcept { return m_id; }
    /// \brief Assign the primary key after an INSERT.
    void setId(int v) { m_id = v; }

    /// \brief Ingredient name; unique across the store room.
    const QString& name() const noexcept { return m_name; }
    /// \brief Set the ingredient name.
    /// \throws core::ValidationException when \p v trims to empty.
    void setName(const QString& v);

    /// \brief Unit of measure, e.g. "kg".
    const QString& unit() const noexcept { return m_unit; }
    /// \brief Set the unit of measure.
    /// \throws core::ValidationException when \p v trims to empty.
    void setUnit(const QString& v);

    /// \brief Quantity currently in store.
    double stockQty() const noexcept { return m_stockQty; }
    /// \brief Set the quantity in store.
    /// \throws core::ValidationException when \p v is negative — mirroring the
    ///         schema's `CHECK (stock_qty >= 0)` so an over-deduction is reported
    ///         as a domain error, not as a driver error.
    void setStockQty(double v);

    /// \brief Reorder threshold.
    double lowThreshold() const noexcept { return m_lowThreshold; }
    /// \brief Set the reorder threshold.
    /// \throws core::ValidationException when \p v is negative.
    void setLowThreshold(double v);

    /// \brief Expiry date; an invalid QDate means "does not expire" / unknown.
    QDate expiryDate() const noexcept { return m_expiry; }
    /// \brief Set the expiry date.
    void setExpiryDate(QDate d) { m_expiry = d; }

    /// \brief Purchase cost of one unit, in NPR.
    core::Money unitCost() const noexcept { return m_unitCost; }
    /// \brief Set the purchase cost per unit.
    /// \throws core::ValidationException when \p c is negative.
    void setUnitCost(core::Money c);

    /// \brief Preferred supplier, or 0 when none is recorded.
    int supplierId() const noexcept { return m_supplierId; }
    /// \brief Set the preferred supplier.
    void setSupplierId(int v) { m_supplierId = v; }

    /// @oop-concept Inline Functions :: the dashboard evaluates this for every ingredient on refresh
    /// \brief Whether stock has fallen to or below the reorder threshold.
    bool isLow() const noexcept { return m_stockQty <= m_lowThreshold; }

    /// @oop-concept Constant Member Functions :: a predicate that answers about, but never touches, the object
    /// \brief Whether this ingredient expires within \p days from today.
    /// \param days Look-ahead window in days.
    /// \return `false` when no expiry date is set.
    bool expiresWithin(int days) const;

private:
    int m_id = 0;                  ///< Primary key.
    int m_supplierId = 0;          ///< Preferred supplier, or 0.
    QString m_name;                ///< Unique ingredient name.
    QString m_unit;                ///< Unit of measure.
    double m_stockQty = 0.0;       ///< Physical quantity in store — NOT money.
    double m_lowThreshold = 0.0;   ///< Reorder threshold — NOT money.
    QDate m_expiry;                ///< Expiry date; invalid when unknown.
    core::Money m_unitCost;        ///< Purchase cost per unit, in paisa.
};

} // namespace aluchop::models

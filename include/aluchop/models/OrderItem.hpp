#pragma once

/// \file
/// \brief OrderItem — one line on an order, frozen at the moment it was added.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QString>

#include "aluchop/core/Money.hpp"

namespace aluchop::models {

/// \brief A quantity of one menu item, with the name and price it had when ordered.
///
/// The name and unit price are **snapshots**, deliberately duplicated out of
/// `menu_items`. If the kitchen re-prices a dish or renames it at 8pm, the bill
/// a guest received at 7pm must still print what they agreed to pay. Storing a
/// bare foreign key would silently rewrite history.
///
/// \warning The unit price is tax-inclusive. lineTotal() is a pure multiplication —
///          nothing is ever added on top.
class OrderItem {
public:
    /// \brief Default constructor — used by OrderRepository before hydration.
    OrderItem() = default;

    /// @oop-concept Parameterised Constructor :: a line item is never valid half-built
    /// \param menuItemId   Source menu row; may be 0 if that dish was later deleted.
    /// \param nameSnapshot Dish name as it stood when ordered.
    /// \param unitPrice    Tax-inclusive unit price as it stood when ordered.
    /// \param qty          Quantity ordered; must be at least 1.
    /// \throws core::ValidationException when \p qty is below 1.
    OrderItem(int menuItemId, QString nameSnapshot, core::Money unitPrice, int qty);

    /// \brief Source menu row; 0 when the dish has since been removed.
    int menuItemId() const noexcept { return m_menuItemId; }

    /// \brief Dish name frozen at order time.
    const QString& name() const noexcept { return m_name; }

    /// \brief Tax-inclusive unit price frozen at order time.
    core::Money unitPrice() const noexcept { return m_unitPrice; }

    /// \brief Quantity ordered; always at least 1.
    int qty() const noexcept { return m_qty; }

    /// \brief Change the quantity.
    /// \throws core::ValidationException when \p q is below 1 — removing a line is
    ///         Order::removeItemAt()'s job, not a quantity of zero.
    void setQty(int q);

    /// \brief Kitchen note for this line, e.g. "no chilli".
    const QString& note() const noexcept { return m_note; }
    /// \brief Set the kitchen note.
    void setNote(const QString& n) { m_note = n; }

    /// @oop-concept Constant Member Functions / Inline Functions :: computed on demand,
    /// never cached, so a quantity change cannot leave a stale total behind
    /// \brief `unitPrice() * qty()`.
    core::Money lineTotal() const noexcept { return m_unitPrice * m_qty; }

private:
    int m_menuItemId = 0;      ///< Source menu row.
    QString m_name;            ///< Name snapshot.
    core::Money m_unitPrice;   ///< Tax-inclusive unit price snapshot.
    int m_qty = 1;             ///< Quantity, at least 1.
    QString m_note;            ///< Per-line kitchen note.
};

} // namespace aluchop::models

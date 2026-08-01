/// \file
/// \brief Implementation of models::OrderItem.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/OrderItem.hpp"

#include <string>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::models {

OrderItem::OrderItem(int menuItemId, QString nameSnapshot, core::Money unitPrice, int qty)
    : m_menuItemId(menuItemId)
    , m_name(nameSnapshot.trimmed())
    , m_unitPrice(unitPrice)
{
    // The name and the unit price are snapshots on purpose: re-pricing a dish at
    // 20:00 must not rewrite the bill a guest settled at 19:00. They are
    // therefore accepted as given and never re-read from menu_items.
    if (m_name.isEmpty())
        throw core::ValidationException("Order line must carry a dish-name snapshot");

    if (unitPrice.isNegative())
        throw core::ValidationException("Order line unit price must not be negative");

    setQty(qty);
}

void OrderItem::setQty(int q)
{
    // Zero is rejected rather than silently treated as a deletion: dropping a
    // line is Order::removeItemAt()'s job, and conflating the two would let a
    // stray spin-box value quietly empty an order.
    if (q < 1)
        throw core::ValidationException(
            "Order line quantity must be at least 1 (got " + std::to_string(q) + ")");
    m_qty = q;
}

} // namespace aluchop::models

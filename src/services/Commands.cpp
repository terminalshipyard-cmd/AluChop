/**
 * @file Commands.cpp
 * @brief Implementation of the undo/redo command stack and its four concrete commands.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The command pattern is the honest way to build undo: each reversible edit knows how to do itself
 * *and* how to take itself back, and `CommandStack` only ever sees the abstract `Command`
 * interface. Every `undo()` below performs a genuine inverse operation against the same service
 * the `execute()` used — there is no snapshot-the-whole-database shortcut and no no-op.
 *
 * Two consequences worth stating, because they are what makes the inverses honest:
 *  - `AddOrderItemCommand` remembers that `Order::addItem` **merges** quantities, so its undo
 *    subtracts the quantity it added and only removes the line when it created that line.
 *  - `RemoveOrderItemCommand` captures the line's identity and quantity *at execute time*, because
 *    once the line is gone there is nothing left to read them from.
 */

#include "aluchop/services/Commands.hpp"

#include <exception>
#include <utility>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/models/OrderItem.hpp"
#include "aluchop/services/InventoryService.hpp"
#include "aluchop/services/MenuService.hpp"
#include "aluchop/services/OrderService.hpp"

namespace aluchop::services {

namespace {

/// @brief Turns a failed `Result` into the throw the Command contract promises.
///
/// Commands signal failure by throwing so that `CommandStack::run` can refuse to push a command
/// that never actually took effect — a history containing an edit that did not happen would make
/// every later undo wrong.
void requireOk(const core::Result<void>& r) {
    if (r.isErr())
        throw core::ValidationException(r.error().toStdString());
}

/// @brief Index of the line carrying @p menuItemId, or `order.itemCount()` when absent.
std::size_t indexOfDish(const models::Order& order, int menuItemId) {
    for (std::size_t i = 0; i < order.itemCount(); ++i)
        if (order[i].menuItemId() == menuItemId)
            return i;
    return order.itemCount();
}

} // namespace

// ---------------------------------------------------------------------------
// AddOrderItemCommand
// ---------------------------------------------------------------------------

AddOrderItemCommand::AddOrderItemCommand(OrderService& svc, int orderId, int menuItemId, int qty)
    : m_svc(svc), m_orderId(orderId), m_menuItemId(menuItemId), m_qty(qty) {}

void AddOrderItemCommand::execute() {
    requireOk(m_svc.addItem(m_orderId, m_menuItemId, m_qty));

    // Where the quantity landed. addItem() merges into an existing line when the dish is already
    // on the order, so the index is discovered after the fact rather than assumed to be the end.
    const auto after = m_svc.order(m_orderId);
    m_addedIndex = after ? indexOfDish(*after, m_menuItemId) : 0;
}

void AddOrderItemCommand::undo() {
    const auto order = m_svc.order(m_orderId);
    if (!order)
        throw core::ValidationException("the order this edit belongs to no longer exists");

    std::size_t index = indexOfDish(*order, m_menuItemId);
    if (index >= order->itemCount())
        index = m_addedIndex;             // fall back to where it landed at execute time
    if (index >= order->itemCount())
        throw core::ValidationException("that line is no longer on the order");

    const int current = (*order)[index].qty();
    if (current > m_qty)
        // The dish was already on the bill: give back exactly the quantity this command added.
        requireOk(m_svc.updateItemQty(m_orderId, index, current - m_qty));
    else
        // This command created the line, so undoing it takes the whole line away again.
        requireOk(m_svc.removeItem(m_orderId, index));
}

QString AddOrderItemCommand::description() const {
    return QStringLiteral("Add %1 x item #%2 to order #%3")
        .arg(m_qty).arg(m_menuItemId).arg(m_orderId);
}

// ---------------------------------------------------------------------------
// RemoveOrderItemCommand
// ---------------------------------------------------------------------------

RemoveOrderItemCommand::RemoveOrderItemCommand(OrderService& svc, int orderId, std::size_t index)
    : m_svc(svc), m_orderId(orderId), m_index(index) {}

void RemoveOrderItemCommand::execute() {
    const auto order = m_svc.order(m_orderId);
    if (!order)
        throw core::ValidationException("the order this edit belongs to no longer exists");
    if (m_index >= order->itemCount())
        throw core::ValidationException("that line is no longer on the order");

    // Captured BEFORE the removal — afterwards there is nothing left to read.
    const models::OrderItem& line = (*order)[m_index];
    m_menuItemId = line.menuItemId();
    m_qty = line.qty();

    requireOk(m_svc.removeItem(m_orderId, m_index));
}

void RemoveOrderItemCommand::undo() {
    if (m_menuItemId == 0 || m_qty < 1)
        throw core::ValidationException(
            "that line referred to a dish that is no longer on the menu, so it cannot be restored");

    // Putting the same dish and quantity back is the true inverse of taking it off. The line may
    // reappear at a different position, because an order is a set of dishes rather than a fixed
    // grid of slots — the money and the kitchen ticket are identical either way.
    requireOk(m_svc.addItem(m_orderId, m_menuItemId, m_qty));
}

QString RemoveOrderItemCommand::description() const {
    return QStringLiteral("Remove line %1 from order #%2")
        .arg(m_index + 1).arg(m_orderId);
}

// ---------------------------------------------------------------------------
// ToggleAvailabilityCommand
// ---------------------------------------------------------------------------

ToggleAvailabilityCommand::ToggleAvailabilityCommand(MenuService& svc, int itemId,
                                                     bool makeAvailable)
    : m_svc(svc), m_itemId(itemId), m_makeAvailable(makeAvailable) {}

void ToggleAvailabilityCommand::execute() {
    requireOk(m_svc.setAvailability(m_itemId, m_makeAvailable));
}

void ToggleAvailabilityCommand::undo() {
    // A boolean flag's inverse is simply the other value — nothing else about the dish moved.
    requireOk(m_svc.setAvailability(m_itemId, !m_makeAvailable));
}

QString ToggleAvailabilityCommand::description() const {
    return m_makeAvailable
               ? QStringLiteral("Put item #%1 back on the menu").arg(m_itemId)
               : QStringLiteral("Take item #%1 off the menu").arg(m_itemId);
}

// ---------------------------------------------------------------------------
// AdjustStockCommand
// ---------------------------------------------------------------------------

AdjustStockCommand::AdjustStockCommand(InventoryService& svc, int ingredientId, double qty,
                                       core::Money unitCost, QString note)
    : m_svc(svc),
      m_ingredientId(ingredientId),
      m_qty(qty),
      m_unitCost(unitCost),
      m_note(std::move(note)) {}

void AdjustStockCommand::execute() {
    requireOk(m_svc.restock(m_ingredientId, m_qty, m_unitCost, m_note));
}

void AdjustStockCommand::undo() {
    // Stock has already been booked in and journalled, so the inverse cannot be a deletion — the
    // ledger is append-only by design. Instead exactly the same quantity is booked back out, with
    // a reason that says why, which returns the quantity to what it was and leaves an honest
    // audit trail of both movements.
    requireOk(m_svc.recordWaste(m_ingredientId, m_qty,
                                QStringLiteral("Undo of restock: %1")
                                    .arg(m_note.isEmpty() ? QStringLiteral("delivery") : m_note)));
}

QString AdjustStockCommand::description() const {
    return QStringLiteral("Book in %1 of ingredient #%2").arg(m_qty).arg(m_ingredientId);
}

// ---------------------------------------------------------------------------
// CommandStack
// ---------------------------------------------------------------------------

/// @oop-concept try / catch :: the stack converts a thrown edit failure into a Result so the GUI
/// can show it — and, crucially, does NOT push a command that threw, so the history can never
/// contain an edit that did not take effect.
core::Result<void> CommandStack::run(std::unique_ptr<Command> cmd) {
    using R = core::Result<void>;

    if (!cmd)
        return R::err(QStringLiteral("There is nothing to do."));

    try {
        cmd->execute();
    } catch (const core::AluChopException& ex) {
        return R::err(QString::fromUtf8(ex.what()));
    } catch (const std::exception& ex) {
        return R::err(QString::fromUtf8(ex.what()));
    }

    // A new action invalidates any future that had been undone away from.
    m_redo.clear();

    /// @oop-concept Object Pointers :: the history is a vector of unique_ptr<Command>, holding a
    /// heterogeneous mix of concrete edits behind one abstract handle.
    m_undo.push_back(std::move(cmd));

    /// @oop-concept Constants :: kMaxDepth is a named limit, so an all-day service cannot grow the
    /// history without bound.
    while (m_undo.size() > kMaxDepth)
        m_undo.erase(m_undo.begin());

    return R::ok();
}

QString CommandStack::undoText() const {
    return m_undo.empty() ? QString() : m_undo.back()->description();
}

QString CommandStack::redoText() const {
    return m_redo.empty() ? QString() : m_redo.back()->description();
}

core::Result<void> CommandStack::undo() {
    using R = core::Result<void>;

    if (m_undo.empty())
        return R::err(QStringLiteral("There is nothing to undo."));

    std::unique_ptr<Command> cmd = std::move(m_undo.back());
    m_undo.pop_back();

    try {
        /// @oop-concept Runtime Polymorphism :: the stack has no idea which concrete edit it is
        /// reversing; the virtual call picks the right inverse.
        cmd->undo();
    } catch (const core::AluChopException& ex) {
        m_undo.push_back(std::move(cmd));      // the edit still stands — put it back
        return R::err(QString::fromUtf8(ex.what()));
    } catch (const std::exception& ex) {
        m_undo.push_back(std::move(cmd));
        return R::err(QString::fromUtf8(ex.what()));
    }

    m_redo.push_back(std::move(cmd));
    while (m_redo.size() > kMaxDepth)
        m_redo.erase(m_redo.begin());

    return R::ok();
}

core::Result<void> CommandStack::redo() {
    using R = core::Result<void>;

    if (m_redo.empty())
        return R::err(QStringLiteral("There is nothing to redo."));

    std::unique_ptr<Command> cmd = std::move(m_redo.back());
    m_redo.pop_back();

    try {
        cmd->execute();
    } catch (const core::AluChopException& ex) {
        m_redo.push_back(std::move(cmd));      // still undone — leave it where it was
        return R::err(QString::fromUtf8(ex.what()));
    } catch (const std::exception& ex) {
        m_redo.push_back(std::move(cmd));
        return R::err(QString::fromUtf8(ex.what()));
    }

    m_undo.push_back(std::move(cmd));
    while (m_undo.size() > kMaxDepth)
        m_undo.erase(m_undo.begin());

    return R::ok();
}

} // namespace aluchop::services

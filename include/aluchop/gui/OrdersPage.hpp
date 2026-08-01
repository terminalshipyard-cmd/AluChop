#pragma once

/**
 * @file OrdersPage.hpp
 * @brief Screen 3 — the point of sale: order queue, kitchen board, editor, split/merge, billing.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QString>

#include "aluchop/gui/Page.hpp"

class QListWidget;
class QTableWidget;

namespace aluchop::gui {

/**
 * @brief The busiest screen in the application — the POS.
 *
 * Three panes: the list of active orders, the kitchen board grouped by status
 * (Pending → Preparing → Ready → Served), and the line editor for the selected order.
 *
 * Everything is a service call:
 *  - creation, line edits, status advancement and cancellation → services::OrderService,
 *  - split and merge → OrderService::splitOrder / mergeOrders, which are backed by
 *    models::Order's copy constructor and `operator+=` respectively,
 *  - billing → BillingDialog, which drives services::BillingService.
 *
 * Line edits are pushed as services::AddOrderItemCommand / RemoveOrderItemCommand onto the
 * shared CommandStack, so Ctrl+Z undoes a mis-tapped dish during service.
 *
 * Inventory is *not* touched here: OrderService deducts recipe ingredients when an order
 * reaches Served, so the deduction rule lives in one place.
 *
 * @oop-concept Copy Constructor (surfaced) :: "Split bill" exists in the UI because
 *              models::Order can be deep-copied with a fresh identity
 * @oop-concept Operator Overloading (surfaced) :: "Merge bills" is models::Order::operator+=
 */
class OrdersPage final : public Page {
    Q_OBJECT
public:
    /// @param ctx composition root; uses orders(), billing(), menu() and commands().
    /// @param parent Qt parent.
    explicit OrdersPage(services::AppContext& ctx, QWidget* parent = nullptr);

    /// @return "Orders".
    QString pageTitle() const override;

    /// Reloads active orders, rebuilds the kitchen board and repopulates the line editor.
    void refresh() override;

public slots:
    /// Starts a new order and focuses the editor. Public because MainWindow binds Ctrl+N to it.
    void onNewOrder();

private slots:
    /// Edits the selected order's header (type, table, customer, notes).
    void onEditOrder();

    /// Cancels the selected order after confirmation; settled orders cannot be cancelled.
    void onCancelOrder();

    /// Adds the chosen menu item and quantity to the selected order (undoable).
    void onAddItemToOrder();

    /// Removes the selected line from the selected order (undoable).
    void onRemoveItemFromOrder();

    /// Sends the order to the kitchen queue (status Pending → Preparing pipeline entry).
    void onSubmitToKitchen();

    /// Advances the selected order one step: Pending → Preparing → Ready → Served.
    void onAdvanceStatus();

    /// Splits the multi-selected item lines into a brand new order (split bill).
    void onSplit();

    /// Merges another open order into the selected one (merge bills).
    void onMerge();

    /// Opens BillingDialog for a Served order: discounts, promo, payment, change, receipt.
    void onBill();

private:
    /// @return the id of the currently selected order, or 0 if none.
    int selectedOrderId() const;

    /// @return the row index of the selected line in the item view, or -1 if none.
    int selectedLineIndex() const;

    QTableWidget* m_orderList = nullptr;  ///< Active orders: No. | Type | Table | Status | Total.
    QListWidget* m_kitchenBoard = nullptr;///< Pending/Preparing/Ready rendered as sections.
    QTableWidget* m_itemsView = nullptr;  ///< Selected order's lines: Item | Qty | Unit | Line total.
};

} // namespace aluchop::gui

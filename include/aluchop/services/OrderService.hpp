#pragma once

/**
 * @file OrderService.hpp
 * @brief The order lifecycle: create, edit, split, merge, cook, serve.
 *
 * The status ladder `Open -> Pending -> Preparing -> Ready -> Served -> Paid` is enforced by
 * `models::Order::setStatus`; this service drives it and hangs the side effects off the right
 * rungs — pushing onto the kitchen queue at Pending, deducting inventory and recording the
 * customer visit at Served.
 */

#include <cstddef>
#include <optional>
#include <vector>

#include <QString>

#include "aluchop/core/Result.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/persistence/MenuRepository.hpp"
#include "aluchop/persistence/OrderRepository.hpp"
#include "aluchop/persistence/TableRepository.hpp"
#include "aluchop/services/KitchenQueue.hpp"

namespace aluchop::services {

class InventoryService;
class CustomerService;
class AuditService;
class NotificationService;

/// @brief Everything that happens to an order between "table 4 sits down" and "table 4 is served".
class OrderService {
public:
    OrderService(persistence::OrderRepository& orders, persistence::MenuRepository& menu,
                 persistence::TableRepository& tables, InventoryService& inventory,
                 CustomerService& customers, AuditService& audit, NotificationService& notify);

    /**
     * @brief Opens a new order and persists it immediately (there is no unsaved draft state).
     * @param tableId required and must be active for `DineIn`; ignored for takeaway/delivery.
     * /// @oop-concept Default Arguments :: a walk-in takeaway needs neither table nor customer
     */
    core::Result<models::Order> createOrder(models::OrderType type, int tableId = 0,
                                            int customerId = 0, int waiterId = 0);

    /// @brief Adds @p qty of a dish; the dish must exist and be available. Quantities merge.
    core::Result<void> addItem(int orderId, int menuItemId, int qty);

    /// @brief Changes the quantity of the line at @p index (qty >= 1).
    core::Result<void> updateItemQty(int orderId, std::size_t index, int qty);

    /// @brief Deletes the line at @p index.
    core::Result<void> removeItem(int orderId, std::size_t index);

    /// @brief Sets the kitchen note carried on the ticket ("no chilli").
    core::Result<void> setOrderNote(int orderId, const QString& note);

    /// @brief Cancels an order and takes it off the kitchen queue.
    core::Result<void> cancelOrder(int orderId);

    /**
     * @brief Splits selected lines out into a brand-new order (the "separate bills, please" case).
     * @param itemIndexes indexes of the lines to move out of the original.
     * @return the newly persisted order; both orders keep the original status.
     *
     * /// @oop-concept Copy Constructor :: the split literally copies the Order object, which is
     * /// why Order's copy ctor resets id/number and deep-copies the item vector
     */
    core::Result<models::Order> splitOrder(int orderId, const std::vector<std::size_t>& itemIndexes);

    /**
     * @brief Merges @p sourceOrderId into @p targetOrderId ("put it all on one bill").
     * @details `target += source`; the source becomes Cancelled with `merged_into` set.
     * /// @oop-concept Operator Overloading :: Order::operator+= IS the merge, expressed in code
     */
    core::Result<void> mergeOrders(int targetOrderId, int sourceOrderId);

    /// @brief `Open -> Pending`: the ticket is fired and joins the kitchen queue.
    core::Result<void> submitToKitchen(int orderId);

    /**
     * @brief Advances one rung: `Pending -> Preparing -> Ready -> Served`.
     * @details On reaching Served the recipes are deducted from stock and, when the order has a
     *          customer, the visit is recorded.
     *
     * /// @oop-concept Multiple Catch :: an InventoryException (not enough stock) is a warning that
     * /// still lets the food leave the pass, while a DatabaseException is returned as an error —
     * /// two failure kinds, two genuinely different recoveries
     */
    core::Result<void> advanceStatus(int orderId);

    /// @return the fully hydrated order, or `std::nullopt`.
    std::optional<models::Order> order(int orderId) const;

    /// @return orders that are neither Paid nor Cancelled.
    std::vector<models::Order> activeOrders() const;

    /// @return orders in one particular status (drives the kitchen board columns).
    std::vector<models::Order> withStatus(models::OrderStatus s) const;

    /// @brief The live kitchen pass.
    /// @oop-concept Return by Reference :: the board reads and mutates the one real queue
    KitchenQueue& kitchenQueue() noexcept { return m_queue; }

    /// @brief Rebuilds the queue from the database (Pending orders, oldest first) — called at startup
    ///        so a restart never loses the pass.
    void rebuildQueue();

private:
    persistence::OrderRepository& m_orders;
    persistence::MenuRepository& m_menu;
    persistence::TableRepository& m_tables;
    InventoryService& m_inventory;
    CustomerService& m_customers;
    AuditService& m_audit;
    NotificationService& m_notify;

    /// @oop-concept Objects as Members :: the pass is part of the service, not a global
    KitchenQueue m_queue;
};

} // namespace aluchop::services

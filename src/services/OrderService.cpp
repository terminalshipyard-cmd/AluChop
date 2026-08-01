/**
 * @file OrderService.cpp
 * @brief Implementation of the order lifecycle: create, edit, split, merge, cook, serve.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The status ladder `Open -> Pending -> Preparing -> Ready -> Served -> Paid` is *enforced* by
 * `models::Order::setStatus` (an illegal rung throws `core::ValidationException`); this service
 * merely drives it and hangs the side effects off the right rungs:
 *
 *  - **Pending**  the ticket joins the kitchen queue (`std::queue` FIFO);
 *  - **Preparing** the ticket leaves the pass (the chef has picked it up);
 *  - **Served**   the recipes are deducted from stock and the guest's visit is recorded.
 *
 * @warning Prices are tax-inclusive throughout. Nothing here computes or adds tax.
 */

#include "aluchop/services/OrderService.hpp"

#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <utility>
#include <vector>

#include <QDateTime>
#include <QStringList>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Logger.hpp"
#include "aluchop/models/MenuItem.hpp"
#include "aluchop/models/Table.hpp"
#include "aluchop/persistence/Database.hpp"
#include "aluchop/services/AuditService.hpp"
#include "aluchop/services/CustomerService.hpp"
#include "aluchop/services/InventoryService.hpp"
#include "aluchop/services/NotificationService.hpp"

namespace aluchop::services {

// ---------------------------------------------------------------------------
// KitchenQueue — the two out-of-line members of the otherwise header-only pass.
//
// `KitchenQueue.hpp` is listed as header-only in docs/ARCHITECTURE.md §2 and there is no
// `KitchenQueue.cpp` in the build manifest, yet snapshot() and remove() are declared without
// bodies. They are defined here, in the translation unit of the one class that owns a
// KitchenQueue by value, rather than by adding a file the manifest does not contain.
// ---------------------------------------------------------------------------

/// @oop-concept Copy Constructor :: the board copies the queue rather than draining the real one —
/// showing the pass must never consume it.
std::vector<int> KitchenQueue::snapshot() const {
    std::vector<int> out;
    out.reserve(m_q.size());

    std::queue<int> copy = m_q;          // copy construction: the live pass is left untouched
    while (!copy.empty()) {
        out.push_back(copy.front());
        copy.pop();
    }
    return out;
}

/// @oop-concept STL Algorithms / Iterators :: the queue is rebuilt without the cancelled id,
/// located with std::find_if over the drained copy.
void KitchenQueue::remove(int orderId) {
    const std::vector<int> ids = snapshot();

    const auto it = std::find_if(ids.begin(), ids.end(),
                                 [orderId](int id) noexcept { return id == orderId; });
    if (it == ids.end())
        return;                           // not on the pass — nothing to do

    std::queue<int> rebuilt;
    for (auto scan = ids.begin(); scan != ids.end(); ++scan)
        if (scan != it) rebuilt.push(*scan);

    m_q = std::move(rebuilt);
}

// ---------------------------------------------------------------------------
// File-local helpers
// ---------------------------------------------------------------------------
namespace {

/// @brief `"order:42"` — the short entity token stamped on audit records.
QString orderTag(int orderId) {
    return QStringLiteral("order:%1").arg(orderId);
}

/// @brief Whether an order may still have its lines edited.
bool isEditable(models::OrderStatus s) noexcept {
    return s == models::OrderStatus::Open || s == models::OrderStatus::Pending;
}

/// @brief The next rung of the kitchen ladder, or the same status when there is none.
models::OrderStatus nextRung(models::OrderStatus s) noexcept {
    switch (s) {
    case models::OrderStatus::Pending:   return models::OrderStatus::Preparing;
    case models::OrderStatus::Preparing: return models::OrderStatus::Ready;
    case models::OrderStatus::Ready:     return models::OrderStatus::Served;
    case models::OrderStatus::Open:
    case models::OrderStatus::Served:
    case models::OrderStatus::Paid:
    case models::OrderStatus::Cancelled:
        break;
    }
    return s;
}

/// @brief Human sentence explaining why a status cannot be advanced.
QString whyNotAdvanceable(models::OrderStatus s) {
    switch (s) {
    case models::OrderStatus::Open:
        return QStringLiteral("Order is still open — submit it to the kitchen first.");
    case models::OrderStatus::Served:
        return QStringLiteral("Order has been served — settle it at the till.");
    case models::OrderStatus::Paid:
        return QStringLiteral("Order is already paid.");
    case models::OrderStatus::Cancelled:
        return QStringLiteral("Order was cancelled.");
    case models::OrderStatus::Pending:
    case models::OrderStatus::Preparing:
    case models::OrderStatus::Ready:
        break;
    }
    return QString();
}

QString exceptionText(const core::AluChopException& ex) {
    return QString::fromUtf8(ex.what());
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

/// @oop-concept Pass by Reference :: every collaborator is owned by AppContext and merely borrowed.
OrderService::OrderService(persistence::OrderRepository& orders, persistence::MenuRepository& menu,
                           persistence::TableRepository& tables, InventoryService& inventory,
                           CustomerService& customers, AuditService& audit,
                           NotificationService& notify)
    : m_orders(orders),
      m_menu(menu),
      m_tables(tables),
      m_inventory(inventory),
      m_customers(customers),
      m_audit(audit),
      m_notify(notify),
      m_queue() {}

// ---------------------------------------------------------------------------
// Creating and editing
// ---------------------------------------------------------------------------

core::Result<models::Order> OrderService::createOrder(models::OrderType type, int tableId,
                                                      int customerId, int waiterId) {
    using R = core::Result<models::Order>;

    try {
        models::Order order;
        order.setType(type);
        order.setCustomerId(customerId);
        order.setWaiterId(waiterId);
        order.setCreatedAt(QDateTime::currentDateTimeUtc());

        if (type == models::OrderType::DineIn) {
            if (tableId <= 0)
                return R::err(QStringLiteral("A dine-in order needs a table."));

            const auto table = m_tables.findById(tableId);
            if (!table)
                return R::err(QStringLiteral("Table %1 does not exist.").arg(tableId));
            if (!table->isActive())
                return R::err(QStringLiteral("Table %1 is out of service.").arg(table->name()));

            order.setTableId(tableId);
        } else {
            order.setTableId(0);   // takeaway and delivery never hold a table
        }

        m_orders.insertOrder(order);   // fills in id and ORD-YYYYMMDD-NNN

        m_audit.log(QStringLiteral("ORDER_NEW"), orderTag(order.id()));
        m_notify.announceDataChanged(QStringLiteral("orders"));
        m_notify.notify(QStringLiteral("Order opened"), order.orderNumber(), 1);

        return R::ok(std::move(order));
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

core::Result<void> OrderService::addItem(int orderId, int menuItemId, int qty) {
    using R = core::Result<void>;

    if (qty < 1)
        return R::err(QStringLiteral("Quantity must be at least 1."));

    try {
        auto order = m_orders.loadOrder(orderId);
        if (!order)
            return R::err(QStringLiteral("Order %1 no longer exists.").arg(orderId));
        if (!isEditable(order->status()))
            return R::err(QStringLiteral("The kitchen has already started this order — "
                                         "its lines can no longer be edited."));

        const auto dish = m_menu.findById(menuItemId);
        if (!dish)
            return R::err(QStringLiteral("That dish is no longer on the menu."));
        if (!dish->isAvailable())
            return R::err(QStringLiteral("%1 is currently unavailable.").arg(dish->name()));

        // The price is a SNAPSHOT: re-pricing the menu tomorrow must not rewrite this bill.
        // It is tax-inclusive and is stored exactly as printed on the menu.
        order->addItem(dish->id(), dish->name(), dish->price(), qty);
        m_orders.updateOrder(*order);

        m_audit.log(QStringLiteral("ORDER_EDIT"), orderTag(orderId), order->subtotal(),
                    QStringLiteral("+%1 x %2").arg(qty).arg(dish->name()));
        m_notify.announceDataChanged(QStringLiteral("orders"));
        return R::ok();
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

core::Result<void> OrderService::updateItemQty(int orderId, std::size_t index, int qty) {
    using R = core::Result<void>;

    if (qty < 1)
        return R::err(QStringLiteral("Quantity must be at least 1 — remove the line instead."));

    try {
        auto order = m_orders.loadOrder(orderId);
        if (!order)
            return R::err(QStringLiteral("Order %1 no longer exists.").arg(orderId));
        if (!isEditable(order->status()))
            return R::err(QStringLiteral("The kitchen has already started this order — "
                                         "its lines can no longer be edited."));
        if (index >= order->itemCount())
            return R::err(QStringLiteral("That line is no longer on the order."));

        /// @oop-concept Subscript Operator :: an order IS a sequence of lines, so the line is
        /// reached with order[index] and mutated in place through the returned reference.
        (*order)[index].setQty(qty);
        m_orders.updateOrder(*order);

        m_audit.log(QStringLiteral("ORDER_EDIT"), orderTag(orderId), order->subtotal(),
                    QStringLiteral("qty -> %1").arg(qty));
        m_notify.announceDataChanged(QStringLiteral("orders"));
        return R::ok();
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    } catch (const std::out_of_range&) {
        return R::err(QStringLiteral("That line is no longer on the order."));
    }
}

core::Result<void> OrderService::removeItem(int orderId, std::size_t index) {
    using R = core::Result<void>;

    try {
        auto order = m_orders.loadOrder(orderId);
        if (!order)
            return R::err(QStringLiteral("Order %1 no longer exists.").arg(orderId));
        if (!isEditable(order->status()))
            return R::err(QStringLiteral("The kitchen has already started this order — "
                                         "its lines can no longer be edited."));
        if (index >= order->itemCount())
            return R::err(QStringLiteral("That line is no longer on the order."));

        const QString removed = (*order)[index].name();
        order->removeItemAt(index);
        m_orders.updateOrder(*order);

        m_audit.log(QStringLiteral("ORDER_EDIT"), orderTag(orderId), order->subtotal(),
                    QStringLiteral("-%1").arg(removed));
        m_notify.announceDataChanged(QStringLiteral("orders"));
        return R::ok();
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    } catch (const std::out_of_range&) {
        return R::err(QStringLiteral("That line is no longer on the order."));
    }
}

core::Result<void> OrderService::setOrderNote(int orderId, const QString& note) {
    using R = core::Result<void>;

    try {
        auto order = m_orders.loadOrder(orderId);
        if (!order)
            return R::err(QStringLiteral("Order %1 no longer exists.").arg(orderId));
        if (order->status() == models::OrderStatus::Paid
            || order->status() == models::OrderStatus::Cancelled)
            return R::err(QStringLiteral("A closed order cannot be annotated."));

        order->setNote(note.trimmed());
        m_orders.updateOrder(*order);

        m_audit.log(QStringLiteral("ORDER_NOTE"), orderTag(orderId));
        m_notify.announceDataChanged(QStringLiteral("orders"));
        return R::ok();
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

core::Result<void> OrderService::cancelOrder(int orderId) {
    using R = core::Result<void>;

    try {
        auto order = m_orders.loadOrder(orderId);
        if (!order)
            return R::err(QStringLiteral("Order %1 no longer exists.").arg(orderId));

        // Order::setStatus is the authority on what may follow what. Asking it first means the
        // database is never touched for a transition the domain model would refuse.
        order->setStatus(models::OrderStatus::Cancelled);

        m_orders.updateStatus(orderId, models::OrderStatus::Cancelled);
        m_queue.remove(orderId);

        m_audit.log(QStringLiteral("ORDER_VOID"), orderTag(orderId));
        m_notify.announceDataChanged(QStringLiteral("orders"));
        m_notify.notify(QStringLiteral("Order cancelled"), order->orderNumber(), 2);
        return R::ok();
    } catch (const core::ValidationException& ex) {
        // An order already in the kitchen cannot simply be voided.
        return R::err(exceptionText(ex));
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

// ---------------------------------------------------------------------------
// Split and merge
// ---------------------------------------------------------------------------

core::Result<models::Order> OrderService::splitOrder(int orderId,
                                                     const std::vector<std::size_t>& itemIndexes) {
    using R = core::Result<models::Order>;

    if (itemIndexes.empty())
        return R::err(QStringLiteral("Select at least one line to move onto the new bill."));

    try {
        auto original = m_orders.loadOrder(orderId);
        if (!original)
            return R::err(QStringLiteral("Order %1 no longer exists.").arg(orderId));
        if (original->status() == models::OrderStatus::Paid
            || original->status() == models::OrderStatus::Cancelled)
            return R::err(QStringLiteral("A closed order cannot be split."));

        const std::set<std::size_t> chosen(itemIndexes.begin(), itemIndexes.end());
        for (const std::size_t i : chosen)
            if (i >= original->itemCount())
                return R::err(QStringLiteral("That line is no longer on the order."));
        if (chosen.size() >= original->itemCount())
            return R::err(QStringLiteral("At least one line must stay on the original bill."));

        /// @oop-concept Copy Constructor :: the split literally copies the Order object. Order's
        /// copy constructor deep-copies the item vector and resets id/order-number, which is
        /// precisely what "a brand-new, unsaved order carrying these lines" means.
        models::Order breakaway(*original);

        // The copy starts life holding *every* line, so strip the ones that stay behind; the
        // original keeps the mirror image. Walking backwards keeps the surviving indexes valid.
        for (std::size_t i = original->itemCount(); i-- > 0;) {
            if (chosen.count(i) == 0)
                breakaway.removeItemAt(i);
            else
                original->removeItemAt(i);
        }

        // Both writes must land together: a persisted breakaway whose parent still holds the
        // same lines would bill the guest twice.
        persistence::Database::instance().transaction([&] {
            m_orders.insertOrder(breakaway);
            m_orders.updateOrder(*original);
        });

        // A ticket that was already on the pass produces a second ticket on the pass.
        if (breakaway.status() == models::OrderStatus::Pending)
            m_queue.push(breakaway.id());

        m_audit.log(QStringLiteral("ORDER_SPLIT"), orderTag(orderId), breakaway.subtotal(),
                    QStringLiteral("-> %1").arg(breakaway.orderNumber()));
        m_notify.announceDataChanged(QStringLiteral("orders"));
        m_notify.notify(QStringLiteral("Bill split"),
                        QStringLiteral("%1 split off %2")
                            .arg(original->orderNumber(), breakaway.orderNumber()),
                        1);

        return R::ok(std::move(breakaway));
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

core::Result<void> OrderService::mergeOrders(int targetOrderId, int sourceOrderId) {
    using R = core::Result<void>;

    if (targetOrderId == sourceOrderId)
        return R::err(QStringLiteral("An order cannot be merged into itself."));

    try {
        auto target = m_orders.loadOrder(targetOrderId);
        auto source = m_orders.loadOrder(sourceOrderId);
        if (!target)
            return R::err(QStringLiteral("Order %1 no longer exists.").arg(targetOrderId));
        if (!source)
            return R::err(QStringLiteral("Order %1 no longer exists.").arg(sourceOrderId));
        if (!isEditable(target->status()) || !isEditable(source->status()))
            return R::err(QStringLiteral("Only open or pending orders can be merged."));
        if (source->itemCount() == 0)
            return R::err(QStringLiteral("%1 has nothing on it to merge.")
                              .arg(source->orderNumber()));

        /// @oop-concept Operator Overloading :: Order::operator+= IS the merge, expressed in code.
        /// Absorbing the lines is a single statement because the domain meaning of "put it all on
        /// one bill" is exactly compound addition.
        *target += *source;

        // The source is voided, not deleted: the audit trail must still be able to explain where
        // those lines came from. setStatus validates the transition before anything is written.
        source->setStatus(models::OrderStatus::Cancelled);

        persistence::Database::instance().transaction([&] {
            m_orders.updateOrder(*target);
            m_orders.updateStatus(sourceOrderId, models::OrderStatus::Cancelled);
            m_orders.markMergedInto(sourceOrderId, targetOrderId);
        });

        m_queue.remove(sourceOrderId);

        m_audit.log(QStringLiteral("ORDER_MERGE"), orderTag(targetOrderId), target->subtotal(),
                    QStringLiteral("<- %1").arg(source->orderNumber()));
        m_notify.announceDataChanged(QStringLiteral("orders"));
        m_notify.notify(QStringLiteral("Bills merged"),
                        QStringLiteral("%1 absorbed into %2")
                            .arg(source->orderNumber(), target->orderNumber()),
                        1);
        return R::ok();
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

// ---------------------------------------------------------------------------
// The kitchen ladder
// ---------------------------------------------------------------------------

core::Result<void> OrderService::submitToKitchen(int orderId) {
    using R = core::Result<void>;

    try {
        auto order = m_orders.loadOrder(orderId);
        if (!order)
            return R::err(QStringLiteral("Order %1 no longer exists.").arg(orderId));
        if (order->itemCount() == 0)
            return R::err(QStringLiteral("An empty order cannot be sent to the kitchen."));

        order->setStatus(models::OrderStatus::Pending);   // throws when the rung is illegal
        m_orders.updateStatus(orderId, models::OrderStatus::Pending);
        m_queue.push(orderId);

        m_audit.log(QStringLiteral("ORDER_FIRE"), orderTag(orderId), order->subtotal(),
                    QStringLiteral("%1 lines").arg(order->itemCount()));
        m_notify.announceDataChanged(QStringLiteral("orders"));
        m_notify.notify(QStringLiteral("Ticket fired"),
                        QStringLiteral("%1 is with the kitchen").arg(order->orderNumber()), 1);
        return R::ok();
    } catch (const core::ValidationException& ex) {
        return R::err(exceptionText(ex));
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

core::Result<void> OrderService::advanceStatus(int orderId) {
    using R = core::Result<void>;

    std::optional<models::Order> order;
    try {
        order = m_orders.loadOrder(orderId);
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
    if (!order)
        return R::err(QStringLiteral("Order %1 no longer exists.").arg(orderId));

    const models::OrderStatus from = order->status();
    const models::OrderStatus to = nextRung(from);
    if (to == from)
        return R::err(whyNotAdvanceable(from));

    try {
        order->setStatus(to);           // the model is the authority on the ladder
    } catch (const core::ValidationException& ex) {
        return R::err(exceptionText(ex));
    }

    /// @oop-concept Multiple Catch :: the two ways serving can go wrong need genuinely different
    /// recoveries. An InventoryException means the store room disagrees with the recipe book — the
    /// plate is already on the guest's table, so the right answer is a loud warning and a
    /// stock-keeping problem to fix later, NOT refusing to serve food that has been cooked. A
    /// DatabaseException means the status change itself did not persist, which the user must be
    /// told about as a failure. Anything else in our hierarchy falls through to a generic error.
    try {
        m_orders.updateStatus(orderId, to);

        if (to == models::OrderStatus::Preparing)
            m_queue.remove(orderId);    // the chef has picked the ticket off the pass

        if (to == models::OrderStatus::Served) {
            // A visit is "we served this guest"; the loyalty POINTS are awarded by
            // BillingService when money actually changes hands, so nothing is credited twice.
            if (order->customerId() != 0) {
                const auto visit = m_customers.recordVisit(order->customerId(), core::Money::zero());
                if (visit.isErr())
                    core::Logger::instance().warn(
                        QStringLiteral("visit not recorded for order %1: %2")
                            .arg(orderId).arg(visit.error()));
            }

            // Recipe-driven stock deduction. May throw InventoryException — caught below.
            m_inventory.deductForOrder(*order);
        }
    } catch (const core::InventoryException& ex) {
        core::Logger::instance().warn(QStringLiteral("stock shortfall on order %1: %2")
                                          .arg(orderId).arg(exceptionText(ex)));
        m_notify.notify(QStringLiteral("Stock problem"), exceptionText(ex), 2);
        m_notify.announceDataChanged(QStringLiteral("orders"));
        m_notify.announceDataChanged(QStringLiteral("inventory"));
        return R::ok();                 // the food still left the pass
    } catch (const core::DatabaseException& ex) {
        core::Logger::instance().error(QStringLiteral("order %1 could not advance: %2")
                                           .arg(orderId).arg(exceptionText(ex)));
        return R::err(exceptionText(ex));
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }

    m_audit.log(QStringLiteral("ORDER_STEP"), orderTag(orderId), order->subtotal(),
                models::toString(to));
    m_notify.announceDataChanged(QStringLiteral("orders"));
    if (to == models::OrderStatus::Ready)
        m_notify.notify(QStringLiteral("Order ready"),
                        QStringLiteral("%1 is on the pass").arg(order->orderNumber()), 1);
    if (to == models::OrderStatus::Served)
        m_notify.announceDataChanged(QStringLiteral("inventory"));

    return R::ok();
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

std::optional<models::Order> OrderService::order(int orderId) const {
    try {
        return m_orders.loadOrder(orderId);
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(QStringLiteral("order %1 could not be read: %2")
                                           .arg(orderId).arg(exceptionText(ex)));
        return std::nullopt;
    }
}

std::vector<models::Order> OrderService::activeOrders() const {
    try {
        return m_orders.activeOrders();
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("active orders could not be read: %1").arg(exceptionText(ex)));
        return {};
    }
}

std::vector<models::Order> OrderService::withStatus(models::OrderStatus s) const {
    try {
        return m_orders.withStatus(s);
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("orders in status %1 could not be read: %2")
                .arg(models::toString(s), exceptionText(ex)));
        return {};
    }
}

void OrderService::rebuildQueue() {
    // Drain whatever is left over, then refill from the database so a restart never loses the pass.
    while (!m_queue.empty())
        m_queue.pop();

    try {
        // withStatus() hands back newest first (`created_at DESC`); the kitchen works oldest
        // first, so the vector is walked backwards.
        const std::vector<models::Order> pending = m_orders.withStatus(models::OrderStatus::Pending);
        for (auto it = pending.rbegin(); it != pending.rend(); ++it)
            m_queue.push(it->id());
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("kitchen queue could not be rebuilt: %1").arg(exceptionText(ex)));
    }
}

} // namespace aluchop::services

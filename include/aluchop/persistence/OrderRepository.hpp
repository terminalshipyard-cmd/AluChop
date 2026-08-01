#pragma once

/**
 * @file OrderRepository.hpp
 * @brief The `orders` header table plus its `order_items` lines.
 *
 * An order is always written as a unit: header and lines go in one transaction, so a half-saved
 * order can never exist. Line rows carry a **snapshot** of the dish name and unit price, which is
 * why editing a menu price later never rewrites history.
 */

#include <optional>
#include <vector>

#include <QDateTime>
#include <QSqlRecord>
#include <QString>

#include "aluchop/models/Enums.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/persistence/Repository.hpp"

namespace aluchop::persistence {

/// @brief Repository over `orders`, owning `order_items` access as well.
class OrderRepository : public Repository<models::Order> {
public:
    OrderRepository();

    /**
     * @brief Persists a brand-new order (header + all lines) in one transaction.
     * @param[in,out] o receives the generated id and the order number `ORD-YYYYMMDD-NNN`.
     * @return the generated primary key.
     * /// @oop-concept Pass by Reference :: the order is mutated in place with its new identity
     */
    int insertOrder(models::Order& o);

    /// @brief Rewrites the header and replaces every line of an existing order (one transaction).
    void updateOrder(const models::Order& o);

    /// @brief Status-only update — the cheap path used by the kitchen board.
    void updateStatus(int orderId, models::OrderStatus s);

    /// @return the fully hydrated order (header **and** items), or `std::nullopt`.
    std::optional<models::Order> loadOrder(int orderId) const;

    /// @return every order in one status, items included, newest first.
    std::vector<models::Order> withStatus(models::OrderStatus s) const;

    /// @return orders that are neither Paid nor Cancelled — the floor's live workload.
    std::vector<models::Order> activeOrders() const;

    /// @brief Half-open window `[from, to)` over `created_at`; drives reports and dashboards.
    std::vector<models::Order> between(const QDateTime& from, const QDateTime& to) const;

    /// @brief Recent orders of one customer — the visit history / favourites source.
    /// /// @oop-concept Default Arguments :: callers usually want the last 20 visits
    std::vector<models::Order> forCustomer(int customerId, int limit = 20) const;

    /// @brief Records that @p sourceOrderId was merged into @p targetOrderId (bill merge trail).
    void markMergedInto(int sourceOrderId, int targetOrderId);

protected:
    /// @return the order header only; @ref loadOrder attaches the line items.
    models::Order fromRecord(const QSqlRecord& rec) const override;

    /// @return `"created_at DESC"` — newest orders first everywhere.
    QString orderByClause() const override;
};

} // namespace aluchop::persistence

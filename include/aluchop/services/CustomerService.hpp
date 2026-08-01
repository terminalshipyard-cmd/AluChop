#pragma once

/**
 * @file CustomerService.hpp
 * @brief The customer database: identity, loyalty, visit history and favourites.
 */

#include <optional>
#include <vector>

#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/core/Result.hpp"
#include "aluchop/models/Customer.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/persistence/CustomerRepository.hpp"
#include "aluchop/persistence/OrderRepository.hpp"

namespace aluchop::services {

class AuditService;
class NotificationService;

/// @brief CRUD plus the loyalty rules that sit on top of the customer table.
class CustomerService {
public:
    CustomerService(persistence::CustomerRepository& customers,
                    persistence::OrderRepository& orders,
                    AuditService& audit, NotificationService& notify);

    /// @return every customer, alphabetically.
    std::vector<models::Customer> all() const;

    /// @brief Search by name or phone.
    std::vector<models::Customer> search(const QString& term) const;

    /// @return the customer with this id, or `std::nullopt`.
    std::optional<models::Customer> byId(int id) const;

    /// @return the customer registered with this phone number, or `std::nullopt`.
    std::optional<models::Customer> byPhone(const QString& phone) const;

    /// @brief Registers a customer. @return the new id, or a validation error (duplicate phone).
    core::Result<int> create(const QString& name, const QString& phone, const QString& email);

    /// @brief Saves edited customer details.
    core::Result<void> update(const models::Customer& c);

    /// @brief Deletes a customer; their past orders keep their snapshots and simply lose the link.
    core::Result<void> remove(int customerId);

    /**
     * @brief Records one more visit and awards loyalty points for the amount spent.
     * @param spent the settled bill total; points earned are `spent.wholeRupees() / 100`.
     *
     * /// @oop-concept Increment Operator :: `++customer` is literally "one more visit" — the
     * /// domain meaning of ++ , which is why Customer overloads it rather than exposing setVisits
     */
    core::Result<void> recordVisit(int customerId, core::Money spent);

    /// @brief Spends loyalty points; refused when the balance is too small.
    core::Result<void> redeemPoints(int customerId, int points);

    /// @brief The customer's recent orders, newest first.
    /// @oop-concept Default Arguments :: the history panel shows the last 20 visits
    std::vector<models::Order> visitHistory(int customerId, int limit = 20) const;

    /// @brief The dishes this customer orders most, by total quantity across their history.
    /// @oop-concept STL Algorithms :: quantities are tallied in a map and ranked with std::sort
    std::vector<QString> favouriteItems(int customerId, int topN = 3) const;

private:
    persistence::CustomerRepository& m_customers;
    persistence::OrderRepository& m_orders;
    AuditService& m_audit;
    NotificationService& m_notify;
};

} // namespace aluchop::services

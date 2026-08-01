#pragma once

/**
 * @file PaymentRepository.hpp
 * @brief The `payments` table and the revenue aggregates computed from it.
 *
 * Every money column is INTEGER paisa; sums are therefore exact, and `core::Money` wraps them the
 * moment they leave SQL. No revenue figure in this application is ever a floating-point number.
 */

#include <utility>
#include <vector>

#include <QDateTime>
#include <QSqlRecord>
#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/models/Payment.hpp"
#include "aluchop/persistence/Repository.hpp"

namespace aluchop::persistence {

/// @brief Repository over `payments`, plus the read-only aggregates the dashboard and reports need.
class PaymentRepository : public Repository<models::Payment> {
public:
    PaymentRepository();

    /// @brief Inserts a settled payment. @return the generated primary key.
    int insert(const models::Payment& p);

    /// @brief Payments in the half-open window `[from, to)`, newest first.
    std::vector<models::Payment> between(const QDateTime& from, const QDateTime& to) const;

    /// @return `SUM(total_paisa)` over the window as exact integer money (zero when empty).
    core::Money revenueBetween(const QDateTime& from, const QDateTime& to) const;

    /**
     * @brief Best-selling dishes inside a window.
     * @return up to @p topN pairs of (menu item name, quantity sold), highest quantity first.
     * /// @oop-concept STL (vector/pair) :: a ranked list is naturally a vector of pairs
     */
    std::vector<std::pair<QString, int>> popularItems(const QDateTime& from, const QDateTime& to,
                                                      int topN) const;

protected:
    models::Payment fromRecord(const QSqlRecord& rec) const override;

    /// @return `"paid_at DESC"`.
    QString orderByClause() const override;
};

} // namespace aluchop::persistence

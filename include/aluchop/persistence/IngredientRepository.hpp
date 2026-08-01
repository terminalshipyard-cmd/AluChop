#pragma once

/**
 * @file IngredientRepository.hpp
 * @brief The `ingredients` table plus the `inventory_transactions` ledger it owns.
 *
 * Stock is never written blind: every change goes through @ref adjustStock, which updates the
 * running quantity and appends a ledger row in the same transaction. That is what makes the
 * inventory history auditable.
 */

#include <tuple>
#include <vector>

#include <QDateTime>
#include <QSqlRecord>
#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/models/Ingredient.hpp"
#include "aluchop/persistence/Repository.hpp"

namespace aluchop::persistence {

/// @brief Repository over `ingredients` with an append-only stock ledger.
class IngredientRepository : public Repository<models::Ingredient> {
public:
    IngredientRepository();

    /// @brief Inserts a stock item. @return the generated primary key.
    int insert(const models::Ingredient& i);

    /// @brief Rewrites every column of an existing stock item.
    void update(const models::Ingredient& i);

    /**
     * @brief Applies a signed stock movement and journals it, atomically.
     * @param deltaQty positive for restock, negative for usage/waste.
     * @param reason one of `RESTOCK`, `USAGE`, `WASTE`, `ADJUST`.
     * @param refOrderId the order that consumed the stock, 0 when not order-driven.
     * @param unitCost purchase price per unit for restocks, zero otherwise.
     * @throws core::DatabaseException when the resulting quantity would break the CHECK (>= 0).
     *
     * /// @oop-concept Default Arguments :: usage and waste need neither a cost nor a note
     */
    void adjustStock(int ingredientId, double deltaQty, const QString& reason,
                     int refOrderId = 0, core::Money unitCost = core::Money(),
                     const QString& note = QString());

    /// @return items at or below their low-stock threshold.
    std::vector<models::Ingredient> lowStock() const;

    /// @return items whose expiry date falls within the next @p days days.
    std::vector<models::Ingredient> expiringWithin(int days) const;

    /**
     * @brief Ledger of one ingredient, newest first.
     * @return tuples of (timestamp, delta quantity, reason, note).
     */
    std::vector<std::tuple<QDateTime, double, QString, QString>>
        history(int ingredientId, int limit = 50) const;

protected:
    models::Ingredient fromRecord(const QSqlRecord& rec) const override;

    /// @return `"name"`.
    QString orderByClause() const override;
};

} // namespace aluchop::persistence

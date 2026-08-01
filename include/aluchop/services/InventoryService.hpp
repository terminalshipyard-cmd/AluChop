#pragma once

/**
 * @file InventoryService.hpp
 * @brief Stock levels, restocking, waste, expiry warnings and recipe-driven deduction.
 *
 * The link between the dining room and the store room is @ref InventoryService::deductForOrder:
 * when an order is served, every line's recipe is multiplied by the quantity sold and taken out of
 * stock, with an `USAGE` ledger row referencing the order. That is why the inventory numbers on
 * the dashboard move on their own as service runs.
 */

#include <tuple>
#include <vector>

#include <QDateTime>
#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/core/Result.hpp"
#include "aluchop/models/Ingredient.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/models/Supplier.hpp"
#include "aluchop/persistence/IngredientRepository.hpp"
#include "aluchop/persistence/MenuRepository.hpp"
#include "aluchop/persistence/SupplierRepository.hpp"

namespace aluchop::services {

class AuditService;
class NotificationService;

/// @brief Everything about ingredients, their suppliers and how service consumes them.
class InventoryService {
public:
    InventoryService(persistence::IngredientRepository& ingredients,
                     persistence::SupplierRepository& suppliers,
                     persistence::MenuRepository& menu,
                     AuditService& audit, NotificationService& notify);

    /// @return every ingredient, alphabetically.
    std::vector<models::Ingredient> all() const;

    /// @brief Adds a stock item. @return its new id, or a validation error.
    core::Result<int> addIngredient(const models::Ingredient& i);

    /// @brief Saves edited stock-item details (unit, thresholds, supplier, expiry).
    core::Result<void> updateIngredient(const models::Ingredient& i);

    /// @brief Books in a delivery (`qty` must be > 0) and journals it with its unit cost.
    /// @oop-concept Default Arguments :: the delivery note is optional
    core::Result<void> restock(int ingredientId, double qty, core::Money unitCost,
                               const QString& note = QString());

    /// @brief Writes off spoiled or spilled stock with a mandatory reason.
    core::Result<void> recordWaste(int ingredientId, double qty, const QString& note);

    /**
     * @brief Deducts every recipe line of a served order from stock.
     * @details Called by `OrderService::advanceStatus` when an order reaches Served. Each line
     *          consumes `recipe quantity x ordered quantity`, logged with reason `USAGE` and the
     *          order id. Falling below the low-stock threshold raises a notification; stock is
     *          clamped at zero with a Warning rather than going negative.
     * @throws core::InventoryException when a recipe references an ingredient that no longer
     *         exists — the caller catches this specifically and still lets the food go out.
     *
     * /// @oop-concept throw :: an inconsistent recipe is an exceptional condition, not a return code
     */
    void deductForOrder(const models::Order& order);

    /// @return ingredients at or below their low-stock threshold.
    std::vector<models::Ingredient> lowStock() const;

    /// @brief Ingredients expiring soon.
    /// @oop-concept Default Arguments :: a one-week horizon is the everyday question
    std::vector<models::Ingredient> expiring(int days = 7) const;

    /// @return the stock ledger of one ingredient: (timestamp, delta, reason, note), newest first.
    std::vector<std::tuple<QDateTime, double, QString, QString>>
        history(int ingredientId, int limit = 50) const;

    /// @return every supplier.
    std::vector<models::Supplier> suppliers() const;

    /// @brief Adds a supplier. @return its new id, or a validation error.
    core::Result<int> addSupplier(const models::Supplier& s);

    /// @brief Saves edited supplier details.
    core::Result<void> updateSupplier(const models::Supplier& s);

private:
    persistence::IngredientRepository& m_ingredients;
    persistence::SupplierRepository& m_suppliers;
    persistence::MenuRepository& m_menu;   ///< read for recipes during deduction
    AuditService& m_audit;
    NotificationService& m_notify;
};

} // namespace aluchop::services

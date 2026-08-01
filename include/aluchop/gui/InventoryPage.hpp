#pragma once

/**
 * @file InventoryPage.hpp
 * @brief Screen 6 — ingredients, stock levels, expiry, suppliers and restocking.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QString>

#include "aluchop/gui/Page.hpp"

class QListWidget;
class QTableWidget;

namespace aluchop::gui {

/**
 * @brief The store room: what is on the shelf, what is running out, who supplies it.
 *
 * services::InventoryService owns every rule — low-stock thresholds, expiry windows, the
 * ingredient ledger — and this screen only displays and requests. Automatic deduction when an
 * order is served happens inside the service (driven by each menu item's recipe), so nothing
 * on this screen has to know about orders.
 *
 * Restock and waste adjustments go through services::AdjustStockCommand on the shared
 * CommandStack: a fat-fingered "+100 kg" is undone with Ctrl+Z, and the reversal is written to
 * the inventory ledger like any other movement.
 *
 * @oop-concept Custom Exceptions (surfaced) :: a deduction that would drive stock negative
 *              raises core::InventoryException in the service and reaches the user here as a
 *              red toast, never as a crash
 */
class InventoryPage final : public Page {
    Q_OBJECT
public:
    /// @param ctx composition root; uses AppContext::inventory() and commands().
    /// @param parent Qt parent.
    explicit InventoryPage(services::AppContext& ctx, QWidget* parent = nullptr);

    /// @return "Inventory".
    QString pageTitle() const override;

    /// Reloads ingredients, suppliers and the low-stock/expiring alert list.
    void refresh() override;

private slots:
    /// Creates an ingredient (name, unit, threshold, supplier, expiry).
    void onAddIngredient();

    /// Edits the selected ingredient's metadata.
    void onEditIngredient();

    /// Adds stock for the selected ingredient with a unit cost and note (undoable).
    void onRestock();

    /// Records spoilage/waste as a negative movement with a reason (undoable).
    void onWaste();

    /// Creates a supplier.
    void onAddSupplier();

    /// Edits the selected supplier.
    void onEditSupplier();

private:
    /// @return the id of the selected ingredient, or 0 when nothing is selected.
    int selectedIngredientId() const;

    /// @return the id of the selected supplier, or 0 when nothing is selected.
    int selectedSupplierId() const;

    QTableWidget* m_ingredients = nullptr; ///< Name | Stock | Unit | Threshold | Expiry | Supplier.
    QTableWidget* m_suppliers = nullptr;   ///< Name | Contact | Phone | Email | Address.
    QListWidget* m_alerts = nullptr;       ///< Low stock and expiring-soon warnings.
};

} // namespace aluchop::gui

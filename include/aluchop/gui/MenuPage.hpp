#pragma once

/**
 * @file MenuPage.hpp
 * @brief Screen 2 — browse, search, sort and filter the 14-category menu; item CRUD.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QString>

#include "aluchop/gui/Page.hpp"

class QComboBox;
class QLineEdit;
class QTableWidget;

namespace aluchop::gui {

/**
 * @brief The restaurant menu, exactly as a manager wants to work with it.
 *
 * Every query goes through services::MenuService::search(term, category, availableOnly, sort)
 * — one service entry point covers the search box, the category filter and the sort selector,
 * so this screen never filters or sorts in memory and never sees SQL.
 *
 * Prices are rendered with core::formatNpr and are **tax-inclusive**: nothing on this screen
 * adds tax to a menu price (SPEC §0).
 *
 * Availability is toggled through services::CommandStack with a
 * services::ToggleAvailabilityCommand, which is what makes Ctrl+Z work globally: the screen
 * pushes intent, the command stack owns the undo semantics.
 *
 * @oop-concept Runtime Polymorphism :: the availability toggle is dispatched as an abstract
 *              services::Command, not as a direct service call
 */
class MenuPage final : public Page {
    Q_OBJECT
public:
    /// @param ctx composition root; uses AppContext::menu() and AppContext::commands().
    /// @param parent Qt parent.
    explicit MenuPage(services::AppContext& ctx, QWidget* parent = nullptr);

    /// @return "Menu".
    QString pageTitle() const override;

    /// Reloads categories and re-runs the current search/filter/sort combination.
    void refresh() override;

private slots:
    /// Search box edited (debounced by the caller's timer) — re-runs the query.
    void onSearch();

    /// Category filter changed, including the "All categories" entry.
    void onCategoryChanged();

    /// Sort selector changed (Name A–Z / Z–A, Price ↑ / ↓, Category).
    void onSortChanged();

    /// Flips the selected item's availability through an undoable command.
    void onToggleAvailability();

    /// Opens the item editor for a new dish and creates it via MenuService::create().
    void onAddItem();

    /// Opens the item editor pre-filled with the selected dish; saves via MenuService::update().
    void onEditItem();

    /// Confirms, then removes via MenuService::remove(); a service error (item referenced by an
    /// open order) is surfaced as a toast, never as a crash.
    void onDeleteItem();

private:
    /// @return the menu-item id of the selected row, or 0 when nothing is selected.
    int selectedItemId() const;

    QLineEdit* m_search = nullptr;    ///< SearchBar instance (see Widgets.hpp).
    QComboBox* m_category = nullptr;  ///< "All" + the 14 categories from core::kMenuCategories.
    QComboBox* m_sort = nullptr;      ///< Maps 1:1 onto services::MenuSort.
    QTableWidget* m_table = nullptr;  ///< Name | Category | Price | Available | Description.
};

} // namespace aluchop::gui

#pragma once

/**
 * @file MenuService.hpp
 * @brief Browsing, searching, sorting and editing the restaurant menu.
 *
 * Menu prices are stored and displayed **tax-inclusive**; no method in this file adds anything on
 * top of a listed price.
 */

#include <vector>

#include <QString>

#include "aluchop/core/Result.hpp"
#include "aluchop/models/MenuItem.hpp"
#include "aluchop/models/RecipeLine.hpp"
#include "aluchop/persistence/MenuRepository.hpp"

namespace aluchop::services {

class AuditService;
class NotificationService;

/// @brief The orderings the menu screen offers.
/// @oop-concept Enumerations :: a closed vocabulary of sort modes, not stringly-typed flags
enum class MenuSort { NameAsc, NameDesc, PriceAsc, PriceDesc, Category };

/// @brief Read and write access to the menu, with the search/sort logic the GUI needs.
class MenuService {
public:
    MenuService(persistence::MenuRepository& repo, AuditService& audit, NotificationService& notify);

    /// @return every dish, grouped by category then name.
    std::vector<models::MenuItem> all() const;

    /**
     * @brief One entry point for the whole menu screen: text search, category filter,
     *        availability filter and ordering.
     * @param term free text matched against name and description; empty matches everything.
     * @param category restrict to one of the 14 categories; empty means all.
     * @param availableOnly hide dishes currently switched off.
     * @param sort ordering applied with `std::sort` (NameAsc uses `MenuItem::operator<`).
     *
     * /// @oop-concept Default Arguments :: one search entry point serves every filter combination
     * /// @oop-concept STL Algorithms :: std::sort with per-mode comparators does the ordering
     */
    std::vector<models::MenuItem> search(const QString& term,
                                         const QString& category = QString(),
                                         bool availableOnly = false,
                                         MenuSort sort = MenuSort::NameAsc) const;

    /// @return the 14 required categories, in `core::kMenuCategories` order.
    std::vector<QString> categories() const;

    /// @brief Adds a dish. @return its new id, or a validation error.
    core::Result<int> create(const models::MenuItem& item);

    /// @brief Saves an edited dish.
    core::Result<void> update(const models::MenuItem& item);

    /// @brief Deletes a dish; refused while an open order still references it.
    core::Result<void> remove(int itemId);

    /// @brief Switches a dish on or off the menu ("86 the salmon"). Undoable via `Commands.hpp`.
    core::Result<void> setAvailability(int itemId, bool available);

    /// @return the ingredient lines that make up one dish.
    std::vector<models::RecipeLine> recipeFor(int menuItemId) const;

    /// @brief Replaces a dish's recipe; this is what makes inventory deduction possible on serve.
    core::Result<void> setRecipe(int menuItemId, const std::vector<models::RecipeLine>& lines);

private:
    persistence::MenuRepository& m_repo;
    AuditService& m_audit;
    NotificationService& m_notify;
};

} // namespace aluchop::services

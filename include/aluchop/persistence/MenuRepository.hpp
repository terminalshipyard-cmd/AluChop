#pragma once

/**
 * @file MenuRepository.hpp
 * @brief The `menu_items` table plus the `recipes` join it owns.
 *
 * Menu prices are stored in `price_paisa` as INTEGER and are **tax-inclusive** — nothing in this
 * layer ever adds a tax component.
 */

#include <vector>

#include <QSqlRecord>
#include <QString>

#include "aluchop/models/MenuItem.hpp"
#include "aluchop/models/RecipeLine.hpp"
#include "aluchop/persistence/Repository.hpp"

namespace aluchop::persistence {

/**
 * @brief Repository over `menu_items`, and sole owner of `recipes` access.
 *
 * The recipe rows live here rather than in a repository of their own because a recipe has no
 * identity outside the dish it belongs to — it is always read and replaced as one set.
 */
class MenuRepository : public Repository<models::MenuItem> {
public:
    MenuRepository();

    /// @brief Inserts a dish. @return the generated primary key.
    int insert(const models::MenuItem& item);

    /// @brief Rewrites every column of an existing dish.
    void update(const models::MenuItem& item);

    /// @return every dish in one of the 14 categories, ordered by name.
    std::vector<models::MenuItem> byCategory(const QString& category) const;

    /// @brief Case-insensitive `LIKE` search across name and description.
    std::vector<models::MenuItem> search(const QString& term) const;

    /// @brief Flips the availability flag ("86 the dish") without touching anything else.
    void setAvailability(int itemId, bool available);

    /// @return the ingredient lines that make up one dish (empty when no recipe is defined).
    std::vector<models::RecipeLine> recipeFor(int menuItemId) const;

    /**
     * @brief Replaces the whole recipe of a dish (delete-all + insert-all) in one transaction.
     * @throws core::DatabaseException — the transaction is rolled back before it propagates.
     */
    void setRecipe(int menuItemId, const std::vector<models::RecipeLine>& lines);

protected:
    models::MenuItem fromRecord(const QSqlRecord& rec) const override;

    /// @return `"category, name"` — the menu reads naturally grouped by course.
    QString orderByClause() const override;
};

} // namespace aluchop::persistence

/**
 * @file MenuService.cpp
 * @brief Browsing, searching, sorting and editing the restaurant menu.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Everything the Menu screen does passes through `search()`: the free-text box, the category
 * combo, the "available only" checkbox and the sort selector are four parameters of one function
 * rather than four near-identical query methods. The database narrows the result set as far as it
 * cheaply can (by category, or by `LIKE`), and the remaining predicates are applied in memory with
 * STL algorithms over a `std::vector<MenuItem>` that is at most a few hundred elements long.
 *
 * Prices are tax-inclusive throughout. No function in this file adds anything to a listed price.
 */

#include "aluchop/services/MenuService.hpp"

#include <algorithm>
#include <iterator>

#include "aluchop/core/AppInfo.hpp"
#include "aluchop/core/Exceptions.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/persistence/OrderRepository.hpp"
#include "aluchop/services/AuditService.hpp"
#include "aluchop/services/NotificationService.hpp"

namespace aluchop::services {

namespace {

/// @brief The `NotificationService` domain name every menu mutation announces.
const QString kDomain = QStringLiteral("menu");

/// @brief `"menu:<id>"` — the audit entity string for a dish.
QString menuEntity(int itemId) {
    return QStringLiteral("menu:%1").arg(itemId);
}

/// @brief Case-insensitive "does this dish match the typed text" test.
bool matchesTerm(const models::MenuItem& item, const QString& term) {
    if (term.isEmpty()) return true;
    return item.name().contains(term, Qt::CaseInsensitive)
        || item.description().contains(term, Qt::CaseInsensitive)
        || item.category().contains(term, Qt::CaseInsensitive);
}

/**
 * @brief Strict weak ordering for one @ref MenuSort mode.
 *
 * Every mode falls back to the case-insensitive name comparison so that two equally-priced dishes
 * (or two dishes in one category) still come out in a stable, predictable order rather than in
 * whatever order SQLite happened to return them.
 */
bool lessBy(MenuSort sort, const models::MenuItem& a, const models::MenuItem& b) {
    switch (sort) {
    case MenuSort::NameAsc:
        /// @oop-concept Relational Operator Overloading :: the default sort IS MenuItem::operator<
        return a < b;
    case MenuSort::NameDesc:
        return b < a;
    case MenuSort::PriceAsc:
        if (a.price() != b.price()) return a.price() < b.price();
        return a < b;
    case MenuSort::PriceDesc:
        if (a.price() != b.price()) return b.price() < a.price();
        return a < b;
    case MenuSort::Category: {
        const int byCategory = QString::compare(a.category(), b.category(), Qt::CaseInsensitive);
        if (byCategory != 0) return byCategory < 0;
        return a < b;
    }
    }
    return a < b;
}

/// @brief Writes a notice and a data-changed hint in one step — every mutation below ends this way.
void announce(NotificationService& notify, const QString& title, const QString& message,
              int level) {
    notify.notify(title, message, level);
    notify.announceDataChanged(kDomain);
}

} // namespace

MenuService::MenuService(persistence::MenuRepository& repo, AuditService& audit,
                         NotificationService& notify)
    : m_repo(repo), m_audit(audit), m_notify(notify) {
    // Pure wiring: the repository, the audit sink and the event bus are all owned by AppContext.
}

// ---------------------------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------------------------

std::vector<models::MenuItem> MenuService::all() const {
    try {
        // MenuRepository::orderByClause() is "category, name", so this already arrives grouped by
        // course — which is exactly how a printed menu reads.
        return m_repo.findAll();
    } catch (const core::AluChopException&) {
        // A read that cannot reach the database yields an empty menu rather than an exception
        // escaping into a GUI slot; the failing write path is where the user is actually told.
        return {};
    }
}

std::vector<models::MenuItem> MenuService::search(const QString& term, const QString& category,
                                                  bool availableOnly, MenuSort sort) const {
    const QString needle = term.trimmed();
    const QString wantedCategory = category.trimmed();

    std::vector<models::MenuItem> pool;
    try {
        // Push the cheapest, most selective filter down into SQL, then finish in memory. Category
        // is preferred over text because it is indexed (idx_menu_category).
        if (!wantedCategory.isEmpty())
            pool = m_repo.byCategory(wantedCategory);
        else if (!needle.isEmpty())
            pool = m_repo.search(needle);
        else
            pool = m_repo.findAll();
    } catch (const core::AluChopException&) {
        return {};
    }

    // Remaining predicates, applied to the superset the database returned.
    /// @oop-concept STL Algorithms :: filtering is std::copy_if over iterators, not a hand loop
    std::vector<models::MenuItem> filtered;
    filtered.reserve(pool.size());
    std::copy_if(pool.begin(), pool.end(), std::back_inserter(filtered),
                 [&](const models::MenuItem& item) {
                     if (availableOnly && !item.isAvailable()) return false;
                     return matchesTerm(item, needle);
                 });

    /// @oop-concept STL Algorithms :: one std::sort driven by a per-mode comparator, so adding a
    /// sort option never means adding another query
    std::sort(filtered.begin(), filtered.end(),
              [sort](const models::MenuItem& a, const models::MenuItem& b) {
                  return lessBy(sort, a, b);
              });

    return filtered;
}

std::vector<QString> MenuService::categories() const {
    // core::kMenuCategories is the single source of truth for SPEC §2's fourteen categories: the
    // seed importer, MenuItem::setCategory's validation and this combo box all read this one array,
    // so a fifteenth section cannot come into existence through a typo.
    std::vector<QString> out;
    out.reserve(core::kMenuCategories.size());
    for (const char* name : core::kMenuCategories)
        out.emplace_back(QString::fromUtf8(name));
    return out;
}

std::vector<models::RecipeLine> MenuService::recipeFor(int menuItemId) const {
    try {
        return m_repo.recipeFor(menuItemId);
    } catch (const core::AluChopException&) {
        return {};
    }
}

// ---------------------------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------------------------

core::Result<int> MenuService::create(const models::MenuItem& item) {
    try {
        if (item.name().trimmed().isEmpty())
            throw core::ValidationException("the dish needs a name", "name");
        if (item.category().trimmed().isEmpty())
            throw core::ValidationException("the dish needs a category", "category");
        if (item.price().isNegative())
            throw core::ValidationException("the price cannot be negative", "price");

        const int newId = m_repo.insert(item);

        m_audit.log(QStringLiteral("MENU_CREATE"), menuEntity(newId), item.price(), item.name());
        announce(m_notify, QStringLiteral("Menu updated"),
                 QStringLiteral("Added \"%1\" to %2.").arg(item.name(), item.category()),
                 static_cast<int>(models::NoticeLevel::Success));
        return core::Result<int>::ok(newId);
    } catch (const core::AluChopException& e) {
        return core::Result<int>::err(QString::fromStdString(e.message()));
    }
}

core::Result<void> MenuService::update(const models::MenuItem& item) {
    try {
        if (item.id() <= 0)
            throw core::ValidationException("that dish has not been saved yet", "id");
        if (!m_repo.findById(item.id()))
            throw core::ValidationException("that dish no longer exists", "id");

        m_repo.update(item);

        m_audit.log(QStringLiteral("MENU_UPDATE"), menuEntity(item.id()), item.price(), item.name());
        announce(m_notify, QStringLiteral("Menu updated"),
                 QStringLiteral("Saved changes to \"%1\".").arg(item.name()),
                 static_cast<int>(models::NoticeLevel::Success));
        return core::Result<void>::ok();
    } catch (const core::AluChopException& e) {
        return core::Result<void>::err(QString::fromStdString(e.message()));
    }
}

core::Result<void> MenuService::remove(int itemId) {
    try {
        const std::optional<models::MenuItem> existing = m_repo.findById(itemId);
        if (!existing)
            throw core::ValidationException("that dish no longer exists", "id");

        // A dish that is sitting on a live ticket may not be deleted. `order_items` keeps a *name
        // and price snapshot*, so the foreign key is ON DELETE SET NULL and the database would
        // happily let the row go — which means this rule has to be enforced here or nowhere.
        //
        // MenuService's frozen constructor gives it only a MenuRepository, so the check reads the
        // order side through a locally-constructed repository. That is safe and layer-legal:
        // repositories are stateless handles over the one process-wide connection owned by
        // persistence::Database, and no SQL text appears in the services layer.
        persistence::OrderRepository orders;
        for (const models::Order& live : orders.activeOrders()) {
            for (const models::OrderItem& line : live.items()) {
                if (line.menuItemId() == itemId) {
                    throw core::ValidationException(
                        ("\"" + existing->name() + "\" is on live order " + live.orderNumber() +
                         " — take it off the menu instead of deleting it").toStdString(),
                        "menuItemId");
                }
            }
        }

        m_repo.removeById(itemId);

        m_audit.log(QStringLiteral("MENU_DELETE"), menuEntity(itemId), existing->price(),
                    existing->name());
        announce(m_notify, QStringLiteral("Menu updated"),
                 QStringLiteral("Removed \"%1\" from the menu.").arg(existing->name()),
                 static_cast<int>(models::NoticeLevel::Warning));
        return core::Result<void>::ok();
    } catch (const core::AluChopException& e) {
        return core::Result<void>::err(QString::fromStdString(e.message()));
    }
}

core::Result<void> MenuService::setAvailability(int itemId, bool available) {
    try {
        const std::optional<models::MenuItem> existing = m_repo.findById(itemId);
        if (!existing)
            throw core::ValidationException("that dish no longer exists", "id");
        if (existing->isAvailable() == available)
            return core::Result<void>::ok();   // already in the requested state — nothing to undo

        m_repo.setAvailability(itemId, available);

        m_audit.log(QStringLiteral("MENU_AVAIL"), menuEntity(itemId), core::Money(),
                    QStringLiteral("%1 -> %2").arg(existing->name(),
                                                   available ? QStringLiteral("available")
                                                             : QStringLiteral("86'd")));
        announce(m_notify,
                 available ? QStringLiteral("Back on the menu") : QStringLiteral("86'd"),
                 available ? QStringLiteral("\"%1\" is available again.").arg(existing->name())
                           : QStringLiteral("\"%1\" is off the menu.").arg(existing->name()),
                 static_cast<int>(available ? models::NoticeLevel::Success
                                            : models::NoticeLevel::Warning));
        return core::Result<void>::ok();
    } catch (const core::AluChopException& e) {
        return core::Result<void>::err(QString::fromStdString(e.message()));
    }
}

core::Result<void> MenuService::setRecipe(int menuItemId,
                                          const std::vector<models::RecipeLine>& lines) {
    try {
        const std::optional<models::MenuItem> existing = m_repo.findById(menuItemId);
        if (!existing)
            throw core::ValidationException("that dish no longer exists", "menuItemId");

        // Every line must belong to this dish, name a real ingredient and consume a positive
        // quantity — the `recipes` CHECK constraint says the same thing, but a driver-level
        // failure gives the user nothing they can act on.
        for (const models::RecipeLine& line : lines) {
            if (line.ingredientId <= 0)
                throw core::ValidationException("a recipe line has no ingredient", "ingredientId");
            if (line.qtyPerServing <= 0.0)
                throw core::ValidationException("a recipe quantity must be greater than zero",
                                                "qtyPerServing");
            if (line.menuItemId != 0 && line.menuItemId != menuItemId)
                throw core::ValidationException("a recipe line belongs to a different dish",
                                                "menuItemId");
        }

        m_repo.setRecipe(menuItemId, lines);

        m_audit.log(QStringLiteral("MENU_RECIPE"), menuEntity(menuItemId), core::Money(),
                    QStringLiteral("%1: %2 line(s)")
                        .arg(existing->name())
                        .arg(static_cast<int>(lines.size())));
        announce(m_notify, QStringLiteral("Recipe saved"),
                 QStringLiteral("\"%1\" now consumes %2 ingredient(s) per serving.")
                     .arg(existing->name())
                     .arg(static_cast<int>(lines.size())),
                 static_cast<int>(models::NoticeLevel::Success));
        return core::Result<void>::ok();
    } catch (const core::AluChopException& e) {
        return core::Result<void>::err(QString::fromStdString(e.message()));
    }
}

} // namespace aluchop::services

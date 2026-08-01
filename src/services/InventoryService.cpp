/**
 * @file InventoryService.cpp
 * @brief Implementation of stock levels, restocking, waste, expiry warnings and the
 *        recipe-driven deduction that links the dining room to the store room.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Every quantity in this file is a `double` and every price is a `core::Money`. That split is
 * deliberate: 0.75 kg of chicken is a physical measurement whose precision is set by a kitchen
 * scale, while NPR 0.75 is money and must stay exact integer paisa.
 */

#include "aluchop/services/InventoryService.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <QDate>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Logger.hpp"
#include "aluchop/models/OrderItem.hpp"
#include "aluchop/models/RecipeLine.hpp"
#include "aluchop/persistence/Database.hpp"
#include "aluchop/services/AuditService.hpp"
#include "aluchop/services/NotificationService.hpp"

namespace aluchop::services {

namespace {

/// Kitchen scales are not exact; without a tolerance a deduction of exactly the remaining stock
/// would intermittently look like an overdraw because of binary rounding.
constexpr double kQtyEpsilon = 1e-9;

QString ingredientTag(int id) {
    return QStringLiteral("ingr:%1").arg(id);
}

QString exceptionText(const core::AluChopException& ex) {
    return QString::fromUtf8(ex.what());
}

/// @brief Value of a physical quantity at a given unit price, as exact paisa.
///
/// The *quantity* is a physical measurement, so it is rounded once to the nearest thousandth of a
/// unit; from that point on the arithmetic is pure `std::int64_t` paisa. No floating-point value
/// ever holds currency, not even transiently.
core::Money valueOf(core::Money unitCost, double qty) {
    const std::int64_t milliQty = std::llround(qty * 1000.0);
    const std::int64_t scaled = unitCost.paisa() * milliQty;   // paisa x 1/1000 units
    const std::int64_t paisa = scaled >= 0 ? (scaled + 500) / 1000 : (scaled - 500) / 1000;
    return core::Money(paisa);
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

/// @oop-concept Pass by Reference :: repositories are owned by AppContext and borrowed here.
InventoryService::InventoryService(persistence::IngredientRepository& ingredients,
                                   persistence::SupplierRepository& suppliers,
                                   persistence::MenuRepository& menu, AuditService& audit,
                                   NotificationService& notify)
    : m_ingredients(ingredients),
      m_suppliers(suppliers),
      m_menu(menu),
      m_audit(audit),
      m_notify(notify) {}

// ---------------------------------------------------------------------------
// Stock items
// ---------------------------------------------------------------------------

std::vector<models::Ingredient> InventoryService::all() const {
    try {
        return m_ingredients.findAll();     // repository orders by name
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("ingredients could not be read: %1").arg(exceptionText(ex)));
        return {};
    }
}

core::Result<int> InventoryService::addIngredient(const models::Ingredient& i) {
    using R = core::Result<int>;

    if (i.name().trimmed().isEmpty())
        return R::err(QStringLiteral("An ingredient needs a name."));
    if (i.unit().trimmed().isEmpty())
        return R::err(QStringLiteral("An ingredient needs a unit of measure (kg, l, pcs...)."));
    if (i.stockQty() < 0.0)
        return R::err(QStringLiteral("Opening stock cannot be negative."));
    if (i.lowThreshold() < 0.0)
        return R::err(QStringLiteral("The reorder threshold cannot be negative."));

    try {
        const int id = m_ingredients.insert(i);
        m_audit.log(QStringLiteral("INGR_NEW"), ingredientTag(id), i.unitCost(), i.name());
        m_notify.announceDataChanged(QStringLiteral("inventory"));
        return R::ok(id);
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

core::Result<void> InventoryService::updateIngredient(const models::Ingredient& i) {
    using R = core::Result<void>;

    if (i.id() <= 0)
        return R::err(QStringLiteral("That ingredient has never been saved."));
    if (i.name().trimmed().isEmpty())
        return R::err(QStringLiteral("An ingredient needs a name."));
    if (i.unit().trimmed().isEmpty())
        return R::err(QStringLiteral("An ingredient needs a unit of measure."));

    try {
        m_ingredients.update(i);
        m_audit.log(QStringLiteral("INGR_EDIT"), ingredientTag(i.id()), i.unitCost(), i.name());
        m_notify.announceDataChanged(QStringLiteral("inventory"));
        return R::ok();
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

core::Result<void> InventoryService::restock(int ingredientId, double qty, core::Money unitCost,
                                             const QString& note) {
    using R = core::Result<void>;

    if (qty <= 0.0)
        return R::err(QStringLiteral("A delivery must book in more than zero."));
    if (unitCost.isNegative())
        return R::err(QStringLiteral("A unit cost cannot be negative."));

    try {
        const auto item = m_ingredients.findById(ingredientId);
        if (!item)
            return R::err(QStringLiteral("That ingredient no longer exists."));

        // Quantity and ledger row move together inside the repository.
        m_ingredients.adjustStock(ingredientId, qty, QStringLiteral("RESTOCK"), 0, unitCost,
                                  note.trimmed());

        // The most recent purchase price becomes the item's standing unit cost, so valuations and
        // the inventory report follow the market rather than the day the item was first entered.
        // The row is re-read first so this write cannot clobber the quantity adjustStock just set.
        if (!unitCost.isZero() && unitCost != item->unitCost()) {
            auto refreshed = m_ingredients.findById(ingredientId);
            if (refreshed) {
                refreshed->setUnitCost(unitCost);
                m_ingredients.update(*refreshed);
            }
        }

        m_audit.log(QStringLiteral("RESTOCK"), ingredientTag(ingredientId), valueOf(unitCost, qty),
                    QStringLiteral("%1 %2 %3").arg(qty).arg(item->unit(), item->name()));
        m_notify.announceDataChanged(QStringLiteral("inventory"));
        m_notify.notify(QStringLiteral("Stock booked in"),
                        QStringLiteral("%1 %2 of %3").arg(qty).arg(item->unit(), item->name()), 1);
        return R::ok();
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

core::Result<void> InventoryService::recordWaste(int ingredientId, double qty,
                                                 const QString& note) {
    using R = core::Result<void>;

    if (qty <= 0.0)
        return R::err(QStringLiteral("A write-off must be more than zero."));
    if (note.trimmed().isEmpty())
        return R::err(QStringLiteral("Written-off stock needs a reason."));

    try {
        const auto item = m_ingredients.findById(ingredientId);
        if (!item)
            return R::err(QStringLiteral("That ingredient no longer exists."));
        if (item->stockQty() + kQtyEpsilon < qty)
            return R::err(QStringLiteral("Only %1 %2 of %3 is in store — %4 cannot be written off.")
                              .arg(item->stockQty()).arg(item->unit(), item->name()).arg(qty));

        m_ingredients.adjustStock(ingredientId, -qty, QStringLiteral("WASTE"), 0, core::Money(),
                                  note.trimmed());

        m_audit.log(QStringLiteral("WASTE"), ingredientTag(ingredientId),
                    valueOf(item->unitCost(), qty),
                    QStringLiteral("%1 %2 %3").arg(qty).arg(item->unit(), item->name()));
        m_notify.announceDataChanged(QStringLiteral("inventory"));
        m_notify.notify(QStringLiteral("Stock written off"),
                        QStringLiteral("%1 %2 of %3").arg(qty).arg(item->unit(), item->name()), 2);
        return R::ok();
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

// ---------------------------------------------------------------------------
// The link between the dining room and the store room
// ---------------------------------------------------------------------------

/**
 * The deduction runs in three phases, and the order of them is the whole design:
 *
 *  1. **Plan** — every line's recipe is multiplied by the quantity sold and folded into one
 *     ingredient-to-quantity map. Two dishes sharing an ingredient therefore draw on it once,
 *     with the combined amount, which is the only way the shortage check can be truthful.
 *  2. **Check** — the plan is validated against current stock *before a single row is written*.
 *     A recipe pointing at an ingredient that no longer exists, or a draw that would push a
 *     quantity below zero, throws `core::InventoryException` naming the offending ingredient.
 *     Nothing has been written at that point, so there is nothing to undo.
 *  3. **Apply** — the whole plan is written inside one `Database::transaction`, so a failure
 *     half-way through leaves stock exactly as it was rather than partially consumed.
 *
 * Only after the transaction commits are low-stock notices raised, because a warning about a
 * quantity that was rolled back would be a lie.
 *
 * @throws core::InventoryException — caught specifically by `OrderService::advanceStatus`, which
 *         warns and still lets the food leave the pass: the plate is already on the table, and
 *         refusing to record that fact would not put the ingredients back.
 */
/// @oop-concept throw :: an inconsistent recipe or an impossible draw is an exceptional condition,
/// not a return code — it must be impossible for a caller to ignore it by accident.
void InventoryService::deductForOrder(const models::Order& order) {
    // --- phase 1: plan -------------------------------------------------------------------
    /// @oop-concept STL (map) :: an ordered ingredient-id to quantity map folds the recipes of
    /// every line into one draw per ingredient.
    std::map<int, double> planned;

    for (const models::OrderItem& line : order.items()) {
        if (line.menuItemId() == 0)
            continue;                     // the dish was deleted; its snapshot has no recipe left

        const std::vector<models::RecipeLine> recipe = m_menu.recipeFor(line.menuItemId());
        for (const models::RecipeLine& r : recipe) {
            if (r.qtyPerServing <= 0.0)
                continue;
            planned[r.ingredientId] += r.qtyPerServing * static_cast<double>(line.qty());
        }
    }

    if (planned.empty())
        return;                           // nothing on this order has a recipe defined

    // --- phase 2: check ------------------------------------------------------------------
    std::vector<std::pair<models::Ingredient, double>> draws;
    draws.reserve(planned.size());

    for (const auto& entry : planned) {
        const auto item = m_ingredients.findById(entry.first);
        if (!item)
            throw core::InventoryException(
                "a recipe refers to an ingredient that is no longer in the store room",
                ingredientTag(entry.first).toStdString());

        if (item->stockQty() + kQtyEpsilon < entry.second)
            throw core::InventoryException(
                QStringLiteral("not enough %1 in store: %2 %3 needed, %4 available")
                    .arg(item->name()).arg(entry.second).arg(item->unit())
                    .arg(item->stockQty()).toStdString(),
                item->name().toStdString());

        draws.emplace_back(*item, entry.second);
    }

    // --- phase 3: apply ------------------------------------------------------------------
    const int refOrderId = order.id();
    persistence::Database::instance().transaction([&] {
        for (const auto& draw : draws)
            m_ingredients.adjustStock(draw.first.id(), -draw.second, QStringLiteral("USAGE"),
                                      refOrderId, core::Money(),
                                      QStringLiteral("served %1").arg(order.orderNumber()));
    });

    core::Money consumedValue;
    for (const auto& draw : draws)
        consumedValue += valueOf(draw.first.unitCost(), draw.second);

    m_audit.log(QStringLiteral("STOCK_USED"), QStringLiteral("order:%1").arg(refOrderId),
                consumedValue, QStringLiteral("%1 ingredients").arg(draws.size()));

    // --- after the commit: tell the floor what has run low --------------------------------
    for (const auto& draw : draws) {
        const auto refreshed = m_ingredients.findById(draw.first.id());
        if (refreshed && refreshed->isLow())
            m_notify.notify(QStringLiteral("Low stock"),
                            QStringLiteral("%1 is down to %2 %3")
                                .arg(refreshed->name()).arg(refreshed->stockQty())
                                .arg(refreshed->unit()),
                            2);
    }
    m_notify.announceDataChanged(QStringLiteral("inventory"));
}

// ---------------------------------------------------------------------------
// Alerts and history
// ---------------------------------------------------------------------------

std::vector<models::Ingredient> InventoryService::lowStock() const {
    try {
        return m_ingredients.lowStock();
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("low-stock list could not be read: %1").arg(exceptionText(ex)));
        return {};
    }
}

std::vector<models::Ingredient> InventoryService::expiring(int days) const {
    if (days < 0)
        return {};
    try {
        return m_ingredients.expiringWithin(days);
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("expiry list could not be read: %1").arg(exceptionText(ex)));
        return {};
    }
}

std::vector<std::tuple<QDateTime, double, QString, QString>>
InventoryService::history(int ingredientId, int limit) const {
    if (ingredientId <= 0 || limit < 1)
        return {};
    try {
        return m_ingredients.history(ingredientId, limit);
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("stock ledger of ingredient %1 could not be read: %2")
                .arg(ingredientId).arg(exceptionText(ex)));
        return {};
    }
}

// ---------------------------------------------------------------------------
// Suppliers
// ---------------------------------------------------------------------------

std::vector<models::Supplier> InventoryService::suppliers() const {
    try {
        return m_suppliers.findAll();
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("suppliers could not be read: %1").arg(exceptionText(ex)));
        return {};
    }
}

core::Result<int> InventoryService::addSupplier(const models::Supplier& s) {
    using R = core::Result<int>;

    if (s.name().trimmed().isEmpty())
        return R::err(QStringLiteral("A supplier needs a name."));

    try {
        const int id = m_suppliers.insert(s);
        m_audit.log(QStringLiteral("SUPP_NEW"), QStringLiteral("supp:%1").arg(id));
        m_notify.announceDataChanged(QStringLiteral("inventory"));
        return R::ok(id);
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

core::Result<void> InventoryService::updateSupplier(const models::Supplier& s) {
    using R = core::Result<void>;

    if (s.id() <= 0)
        return R::err(QStringLiteral("That supplier has never been saved."));
    if (s.name().trimmed().isEmpty())
        return R::err(QStringLiteral("A supplier needs a name."));

    try {
        m_suppliers.update(s);
        m_audit.log(QStringLiteral("SUPP_EDIT"), QStringLiteral("supp:%1").arg(s.id()));
        m_notify.announceDataChanged(QStringLiteral("inventory"));
        return R::ok();
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

} // namespace aluchop::services

/**
 * @file MenuRepository.cpp
 * @brief The `menu_items` table plus the `recipes` join it owns.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Prices live in `price_paisa` as INTEGER and are **tax-inclusive**: what is stored is what is
 * printed on the physical menu and what the guest pays. Nothing here adds a tax component, and
 * nothing converts a price through `double` on the way in or out.
 */

#include "aluchop/persistence/MenuRepository.hpp"

#include <QSqlQuery>
#include <QVariant>

#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {
namespace {

/// Escapes `%`, `_` and the escape character itself so a search term is treated as literal text.
QString likePattern(const QString& term) {
    QString t = term;
    t.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    t.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    t.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    QString p;
    p.reserve(t.size() + 2);
    p += QLatin1Char('%');
    p += t;
    p += QLatin1Char('%');
    return p;
}

} // namespace

MenuRepository::MenuRepository() : Repository(QStringLiteral("menu_items")) {}

int MenuRepository::insert(const models::MenuItem& item) {
    // `created_at` is left to the column default so the database, not the caller's clock, stamps it.
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("INSERT INTO menu_items "
                       "(name, category, price_paisa, description, image_path, is_available) "
                       "VALUES (?, ?, ?, ?, ?, ?)"),
        { item.name(), item.category(), static_cast<qlonglong>(item.price().paisa()),
          item.description(), item.imagePath(), item.isAvailable() ? 1 : 0 });
    return q.lastInsertId().toInt();
}

void MenuRepository::update(const models::MenuItem& item) {
    Database::instance().prepared(
        QStringLiteral("UPDATE menu_items SET name = ?, category = ?, price_paisa = ?, "
                       "description = ?, image_path = ?, is_available = ? WHERE id = ?"),
        { item.name(), item.category(), static_cast<qlonglong>(item.price().paisa()),
          item.description(), item.imagePath(), item.isAvailable() ? 1 : 0, item.id() });
}

std::vector<models::MenuItem> MenuRepository::byCategory(const QString& category) const {
    std::vector<models::MenuItem> out;
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM menu_items WHERE category = ? ORDER BY name"), { category });
    while (q.next()) out.push_back(fromRecord(q.record()));
    return out;
}

std::vector<models::MenuItem> MenuRepository::search(const QString& term) const {
    std::vector<models::MenuItem> out;
    const QString pattern = likePattern(term.trimmed());
    // SQLite's LIKE is already case-insensitive for ASCII, which is what the menu is written in.
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM menu_items "
                       "WHERE name LIKE ? ESCAPE '\\' OR description LIKE ? ESCAPE '\\' "
                       "ORDER BY category, name"),
        { pattern, pattern });
    while (q.next()) out.push_back(fromRecord(q.record()));
    return out;
}

void MenuRepository::setAvailability(int itemId, bool available) {
    // Deliberately a single-column write: "86 the dish" must not be able to clobber a price that
    // somebody else edited between this screen's last refresh and this click.
    Database::instance().prepared(
        QStringLiteral("UPDATE menu_items SET is_available = ? WHERE id = ?"),
        { available ? 1 : 0, itemId });
}

std::vector<models::RecipeLine> MenuRepository::recipeFor(int menuItemId) const {
    std::vector<models::RecipeLine> lines;
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT menu_item_id, ingredient_id, qty_per_serving FROM recipes "
                       "WHERE menu_item_id = ? ORDER BY id"),
        { menuItemId });
    while (q.next()) {
        models::RecipeLine line;
        line.menuItemId = q.value(0).toInt();
        line.ingredientId = q.value(1).toInt();
        line.qtyPerServing = q.value(2).toDouble();   // a physical quantity — never money
        lines.push_back(line);
    }
    return lines;
}

void MenuRepository::setRecipe(int menuItemId, const std::vector<models::RecipeLine>& lines) {
    Database& db = Database::instance();
    // Replace-all inside one transaction: a recipe is only meaningful as a complete set, so a
    // failure half-way through must leave the previous recipe standing rather than a partial one.
    db.transaction([&] {
        db.prepared(QStringLiteral("DELETE FROM recipes WHERE menu_item_id = ?"), { menuItemId });
        for (const models::RecipeLine& line : lines) {
            db.prepared(QStringLiteral("INSERT INTO recipes (menu_item_id, ingredient_id, qty_per_serving) "
                                       "VALUES (?, ?, ?)"),
                        { menuItemId, line.ingredientId, line.qtyPerServing });
        }
    });
}

models::MenuItem MenuRepository::fromRecord(const QSqlRecord& rec) const {
    models::MenuItem item(rec.value(QStringLiteral("id")).toInt(),
                          rec.value(QStringLiteral("name")).toString(),
                          rec.value(QStringLiteral("category")).toString(),
                          core::Money(rec.value(QStringLiteral("price_paisa")).toLongLong()),
                          rec.value(QStringLiteral("description")).toString());
    item.setImagePath(rec.value(QStringLiteral("image_path")).toString());
    item.setAvailable(rec.value(QStringLiteral("is_available")).toInt() != 0);
    return item;
}

QString MenuRepository::orderByClause() const { return QStringLiteral("category, name"); }

} // namespace aluchop::persistence

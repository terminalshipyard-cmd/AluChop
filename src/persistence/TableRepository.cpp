/**
 * @file TableRepository.cpp
 * @brief CRUD for the `tables` table (physical dining tables).
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include "aluchop/persistence/TableRepository.hpp"

#include <QSqlQuery>
#include <QVariant>

#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {

TableRepository::TableRepository() : Repository(QStringLiteral("tables")) {}

int TableRepository::insert(const models::Table& t) {
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("INSERT INTO tables (name, capacity, is_active) VALUES (?, ?, ?)"),
        { t.name(), t.capacity(), t.isActive() ? 1 : 0 });
    return q.lastInsertId().toInt();
}

void TableRepository::update(const models::Table& t) {
    Database::instance().prepared(
        QStringLiteral("UPDATE tables SET name = ?, capacity = ?, is_active = ? WHERE id = ?"),
        { t.name(), t.capacity(), t.isActive() ? 1 : 0, t.id() });
}

std::vector<models::Table> TableRepository::activeWithCapacityAtLeast(int guests) const {
    // Out-of-service tables are excluded here rather than in the caller, so no booking path can
    // ever offer a table that has been taken off the floor.
    std::vector<models::Table> out;
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM tables WHERE is_active = 1 AND capacity >= ? ORDER BY name"),
        { guests });
    while (q.next()) out.push_back(fromRecord(q.record()));
    return out;
}

models::Table TableRepository::fromRecord(const QSqlRecord& rec) const {
    models::Table t(rec.value(QStringLiteral("id")).toInt(),
                    rec.value(QStringLiteral("name")).toString(),
                    rec.value(QStringLiteral("capacity")).toInt());
    t.setActive(rec.value(QStringLiteral("is_active")).toInt() != 0);
    return t;
}

QString TableRepository::orderByClause() const { return QStringLiteral("name"); }

} // namespace aluchop::persistence

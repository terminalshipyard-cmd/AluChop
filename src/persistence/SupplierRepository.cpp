/**
 * @file SupplierRepository.cpp
 * @brief CRUD for the `suppliers` table.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include "aluchop/persistence/SupplierRepository.hpp"

#include <QSqlQuery>
#include <QVariant>

#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {

SupplierRepository::SupplierRepository() : Repository(QStringLiteral("suppliers")) {}

int SupplierRepository::insert(const models::Supplier& s) {
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("INSERT INTO suppliers (name, phone, email, address) VALUES (?, ?, ?, ?)"),
        { s.name(), s.phone(), s.email(), s.address() });
    return q.lastInsertId().toInt();
}

void SupplierRepository::update(const models::Supplier& s) {
    Database::instance().prepared(
        QStringLiteral("UPDATE suppliers SET name = ?, phone = ?, email = ?, address = ? WHERE id = ?"),
        { s.name(), s.phone(), s.email(), s.address(), s.id() });
}

/// @oop-concept Method Overriding :: the generic CRUD skeleton is completed by this one hook
models::Supplier SupplierRepository::fromRecord(const QSqlRecord& rec) const {
    return models::Supplier(rec.value(QStringLiteral("id")).toInt(),
                            rec.value(QStringLiteral("name")).toString(),
                            rec.value(QStringLiteral("phone")).toString(),
                            rec.value(QStringLiteral("email")).toString(),
                            rec.value(QStringLiteral("address")).toString());
}

QString SupplierRepository::orderByClause() const { return QStringLiteral("name"); }

} // namespace aluchop::persistence

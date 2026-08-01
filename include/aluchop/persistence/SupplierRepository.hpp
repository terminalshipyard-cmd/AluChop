#pragma once

/**
 * @file SupplierRepository.hpp
 * @brief CRUD for the `suppliers` table.
 */

#include <QSqlRecord>
#include <QString>

#include "aluchop/models/Supplier.hpp"
#include "aluchop/persistence/Repository.hpp"

namespace aluchop::persistence {

/// @brief Repository over `suppliers`; ingredients reference these rows.
class SupplierRepository : public Repository<models::Supplier> {
public:
    SupplierRepository();

    /// @brief Inserts a supplier. @return the generated primary key.
    int insert(const models::Supplier& s);

    /// @brief Rewrites every column of an existing supplier.
    void update(const models::Supplier& s);

protected:
    models::Supplier fromRecord(const QSqlRecord& rec) const override;

    /// @return `"name"`.
    QString orderByClause() const override;
};

} // namespace aluchop::persistence

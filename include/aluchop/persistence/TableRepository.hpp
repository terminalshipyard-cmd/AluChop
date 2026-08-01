#pragma once

/**
 * @file TableRepository.hpp
 * @brief CRUD for the `tables` table (physical dining tables).
 */

#include <vector>

#include <QSqlRecord>
#include <QString>

#include "aluchop/models/Table.hpp"
#include "aluchop/persistence/Repository.hpp"

namespace aluchop::persistence {

/// @brief Repository over `tables`; reservations and dine-in orders point at these rows.
class TableRepository : public Repository<models::Table> {
public:
    TableRepository();

    /// @brief Inserts a table. @return the generated primary key.
    int insert(const models::Table& t);

    /// @brief Rewrites every column of an existing table.
    void update(const models::Table& t);

    /// @return active tables that can seat at least @p guests — the candidate set for booking.
    std::vector<models::Table> activeWithCapacityAtLeast(int guests) const;

protected:
    models::Table fromRecord(const QSqlRecord& rec) const override;

    /// @return `"name"` — T1, T2, ... read in floor order.
    QString orderByClause() const override;
};

} // namespace aluchop::persistence

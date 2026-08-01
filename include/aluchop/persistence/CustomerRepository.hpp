#pragma once

/**
 * @file CustomerRepository.hpp
 * @brief CRUD and lookups for the `customers` table (loyalty database).
 */

#include <optional>
#include <vector>

#include <QSqlRecord>
#include <QString>

#include "aluchop/models/Customer.hpp"
#include "aluchop/persistence/Repository.hpp"

namespace aluchop::persistence {

/// @brief Repository over `customers`; also persists loyalty points and visit counts.
class CustomerRepository : public Repository<models::Customer> {
public:
    CustomerRepository();

    /// @brief Inserts a customer. @return the generated primary key.
    /// @throws core::DatabaseException when the phone number is already registered (UNIQUE).
    int insert(const models::Customer& c);

    /// @brief Rewrites identity **and** the loyalty_points / visits counters.
    void update(const models::Customer& c);

    /// @return the customer registered with this phone number, or `std::nullopt`.
    std::optional<models::Customer> byPhone(const QString& phone) const;

    /// @brief Case-insensitive `LIKE` search across name and phone.
    std::vector<models::Customer> search(const QString& term) const;

protected:
    models::Customer fromRecord(const QSqlRecord& rec) const override;

    /// @return `"name"` — the customer list is alphabetical.
    QString orderByClause() const override;
};

} // namespace aluchop::persistence

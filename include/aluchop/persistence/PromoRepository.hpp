#pragma once

/**
 * @file PromoRepository.hpp
 * @brief CRUD and code lookup for the `promos` table.
 */

#include <optional>

#include <QSqlRecord>
#include <QString>

#include "aluchop/models/Promo.hpp"
#include "aluchop/persistence/Repository.hpp"

namespace aluchop::persistence {

/// @brief Repository over `promos`; validity is decided by `models::Promo`, not by SQL.
class PromoRepository : public Repository<models::Promo> {
public:
    PromoRepository();

    /// @brief Inserts a promo code. @return the generated primary key.
    int insert(const models::Promo& p);

    /// @brief Rewrites every column of an existing promo.
    void update(const models::Promo& p);

    /**
     * @brief Case-insensitive code lookup.
     * @return the promo, or `std::nullopt` when the code does not exist.
     * @note Existence is not validity — the caller still asks `Promo::isValidOn(...)`.
     */
    std::optional<models::Promo> byCode(const QString& code) const;

protected:
    models::Promo fromRecord(const QSqlRecord& rec) const override;

    /// @return `"code"`.
    QString orderByClause() const override;
};

} // namespace aluchop::persistence

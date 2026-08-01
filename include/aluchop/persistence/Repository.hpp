#pragma once

/**
 * @file Repository.hpp
 * @brief Generic CRUD skeleton shared by every table-backed repository.
 *
 * `Repository<T>` supplies the four operations that are identical for every id-keyed table
 * (`findAll`, `findById`, `count`, `removeById`). The only thing that genuinely differs per
 * entity — turning a `QSqlRecord` into a domain object — is left as a pure virtual hook, so a
 * concrete repository writes hydration once and inherits the rest.
 *
 * Header-only by design: every translation unit that instantiates `Repository<T>` must see the
 * member definitions (docs/ARCHITECTURE.md §8).
 */

#include <optional>
#include <utility>
#include <vector>

#include <QSqlQuery>
#include <QSqlRecord>
#include <QString>
#include <QVariantList>

#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {

/**
 * @brief Table-generic CRUD base.
 * @tparam T the domain model this repository hydrates.
 *
 * /// @oop-concept Class Template :: one generic CRUD skeleton, specialised per entity by fromRecord()
 * /// @oop-concept Abstract Classes :: fromRecord() is pure virtual — Repository alone is not usable
 */
template <typename T>
class Repository {
public:
    /// @param tableName SQL table this repository owns; supplied by each concrete subclass.
    explicit Repository(QString tableName) : m_table(std::move(tableName)) {}

    /// @oop-concept Destructor :: virtual, so a concrete repository held through a Repository<T>
    /// handle is still destroyed completely
    virtual ~Repository() = default;

    /**
     * @brief Loads every row of the table, ordered by orderByClause().
     * /// @oop-concept STL (vector) :: result sets are returned as std::vector of value objects
     * @throws core::DatabaseException propagated from Database::exec.
     */
    std::vector<T> findAll() const {
        std::vector<T> out;
        QSqlQuery q = Database::instance().exec(
            QStringLiteral("SELECT * FROM %1 ORDER BY %2").arg(m_table, orderByClause()));
        while (q.next()) out.push_back(fromRecord(q.record()));
        return out;
    }

    /**
     * @brief Loads a single row by primary key.
     * @return the hydrated object, or `std::nullopt` when no such id exists.
     */
    std::optional<T> findById(int id) const {
        QSqlQuery q = Database::instance().prepared(
            QStringLiteral("SELECT * FROM %1 WHERE id = ?").arg(m_table), { id });
        if (q.next()) return fromRecord(q.record());
        return std::nullopt;
    }

    /// @return number of rows currently in the table.
    int count() const {
        QSqlQuery q = Database::instance().exec(
            QStringLiteral("SELECT COUNT(*) FROM %1").arg(m_table));
        return q.next() ? q.value(0).toInt() : 0;
    }

    /// @brief Deletes the row with the given primary key (no-op when absent).
    void removeById(int id) {
        Database::instance().prepared(
            QStringLiteral("DELETE FROM %1 WHERE id = ?").arg(m_table), { id });
    }

protected:
    /**
     * @brief Builds one domain object from one result row.
     * /// @oop-concept Pure Virtual Functions :: hydration is the only entity-specific step
     */
    virtual T fromRecord(const QSqlRecord& rec) const = 0;

    /// @brief Default ordering column list; overridden where a nicer default exists (e.g. "name").
    /// @oop-concept Method Overriding :: subclasses refine only the ordering they care about
    virtual QString orderByClause() const { return QStringLiteral("id"); }

    const QString m_table;   ///< immutable table name, fixed at construction
};

} // namespace aluchop::persistence

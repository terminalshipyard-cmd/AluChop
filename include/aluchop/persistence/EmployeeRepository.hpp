#pragma once

/**
 * @file EmployeeRepository.hpp
 * @brief The `employees` table plus the `attendance` table it owns.
 *
 * Two different shapes come out of here on purpose:
 *  - `Repository<Employee>::findAll()` yields base-sliced `Employee` values, which is exactly what
 *    a table view or an edit form needs;
 *  - @ref makeTyped / @ref allTyped build the *concrete* role on the heap (Waiter, Chef, Manager,
 *    Admin) so that payroll can call the virtual `monthlyPay()` and get each role's own rule.
 */

#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include <QDate>
#include <QSqlRecord>
#include <QString>
#include <QTime>

#include "aluchop/models/Employee.hpp"
#include "aluchop/persistence/Repository.hpp"

namespace aluchop::persistence {

/// @brief Repository over `employees` (base slice) with a polymorphic role factory on top.
class EmployeeRepository : public Repository<models::Employee> {
public:
    EmployeeRepository();

    /// @brief Inserts a staff member; the `position` column is taken from `e.position()`.
    /// @return the generated primary key.
    int insert(const models::Employee& e);

    /// @brief Rewrites every mutable column of an existing staff member.
    void update(const models::Employee& e);

    /**
     * @brief Builds the concrete role object for one employee row.
     * @return `Waiter`/`Chef`/`Manager`/`Admin` on the heap, or `nullptr` when the id is unknown.
     *
     * /// @oop-concept Dynamic Objects :: the runtime type is only known from the position column,
     * /// so the object must be created with `new` (via std::make_unique) rather than by value
     * /// @oop-concept Runtime Polymorphism :: callers hold Employee* and get role-specific pay
     */
    std::unique_ptr<models::Employee> makeTyped(int employeeId) const;

    /// @brief Same factory applied to every row.
    /// @oop-concept Object Pointers :: a heterogeneous staff list is a vector of base pointers
    std::vector<std::unique_ptr<models::Employee>> allTyped() const;

    /**
     * @brief Records (or corrects) one attendance row — UPSERT on (employee_id, work_date).
     * @param status one of `PRESENT`, `ABSENT`, `LEAVE`.
     * /// @oop-concept Default Arguments :: check-in/out are optional for ABSENT and LEAVE days
     */
    void markAttendance(int employeeId, QDate day, const QString& status,
                        QTime checkIn = QTime(), QTime checkOut = QTime());

    /**
     * @brief Attendance rows of one employee for one calendar month.
     * @return tuples of (work_date, status, check_in, check_out), oldest first.
     */
    std::vector<std::tuple<QDate, QString, QTime, QTime>>
        attendanceFor(int employeeId, int year, int month) const;

protected:
    /// @return a base-sliced `Employee` — see @ref makeTyped for the polymorphic form.
    models::Employee fromRecord(const QSqlRecord& rec) const override;

    /// @return `"name"`.
    QString orderByClause() const override;
};

} // namespace aluchop::persistence

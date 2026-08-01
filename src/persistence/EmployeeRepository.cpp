/**
 * @file EmployeeRepository.cpp
 * @brief The `employees` table plus the `attendance` table it owns.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Two shapes come out of this one table on purpose:
 *
 *  - @ref fromRecord builds a base-sliced `models::Employee`, which is all a list view or an edit
 *    form needs — there the role is just a word in a column;
 *  - @ref makeTyped / @ref allTyped build the *concrete* role on the heap, so `monthlyPay()`
 *    dispatches virtually and payroll gets each role's own rule with no `if (position == …)` chain
 *    anywhere outside this file.
 *
 * The `position` column is the only thing that knows which class to build, and it is the reason the
 * factory has to allocate dynamically: the type is not known until the row has been read.
 */

#include "aluchop/persistence/EmployeeRepository.hpp"

#include <QSqlQuery>
#include <QVariant>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Money.hpp"
#include "aluchop/models/Admin.hpp"
#include "aluchop/models/Chef.hpp"
#include "aluchop/models/Manager.hpp"
#include "aluchop/models/Waiter.hpp"
#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {
namespace {

constexpr auto kDateFormat = "yyyy-MM-dd";
constexpr auto kTimeFormat = "HH:mm";

QVariant dateOrNull(QDate d) {
    return d.isValid() ? QVariant(d.toString(QString::fromLatin1(kDateFormat))) : QVariant();
}

QDate dateFromDb(const QVariant& v) {
    if (v.isNull()) return QDate();
    return QDate::fromString(v.toString(), QString::fromLatin1(kDateFormat));
}

/// An ABSENT or LEAVE day genuinely has no clock times, so those columns are NULL rather than 00:00.
QVariant timeOrNull(QTime t) {
    return t.isValid() ? QVariant(t.toString(QString::fromLatin1(kTimeFormat))) : QVariant();
}

QTime timeFromDb(const QVariant& v) {
    if (v.isNull()) return QTime();
    return QTime::fromString(v.toString(), QString::fromLatin1(kTimeFormat));
}

/// Fields every role shares but which no role constructor takes.
void applyCommonColumns(models::Employee& e, const QSqlRecord& rec) {
    e.setHiredDate(dateFromDb(rec.value(QStringLiteral("hired_date"))));
    e.setActive(rec.value(QStringLiteral("is_active")).toInt() != 0);
    e.setPerformanceRating(rec.value(QStringLiteral("performance_rating")).toInt());
}

/**
 * @brief Builds the concrete role named by the `position` column.
 *
 * /// @oop-concept Dynamic Objects :: the class to instantiate is a runtime fact read out of a
 * /// column, so the object must be created with new (through std::make_unique) — there is no way
 * /// to name the type at compile time
 * /// @oop-concept Runtime Polymorphism :: every caller holds Employee*, yet monthlyPay() runs the
 * /// waiter's, the chef's, the manager's or the admin's rule
 */
std::unique_ptr<models::Employee> buildTyped(const QSqlRecord& rec) {
    const int id = rec.value(QStringLiteral("id")).toInt();
    const QString name = rec.value(QStringLiteral("name")).toString();
    const QString phone = rec.value(QStringLiteral("phone")).toString();
    const QString email = rec.value(QStringLiteral("email")).toString();
    const QString position = rec.value(QStringLiteral("position")).toString();
    const core::Money salary(rec.value(QStringLiteral("salary_paisa")).toLongLong());
    const QString shift = rec.value(QStringLiteral("shift")).toString();

    std::unique_ptr<models::Employee> e;
    if (position == QLatin1String("WAITER"))
        e = std::make_unique<models::Waiter>(id, name, phone, email, salary, shift);
    else if (position == QLatin1String("CHEF"))
        e = std::make_unique<models::Chef>(id, name, phone, email, salary, shift);
    else if (position == QLatin1String("MANAGER"))
        e = std::make_unique<models::Manager>(id, name, phone, email, salary, shift);
    else if (position == QLatin1String("ADMIN"))
        e = std::make_unique<models::Admin>(id, name, phone, email, salary, shift);
    else
        throw core::DatabaseException("unknown employee position token",
                                      position.toStdString());

    e->setPosition(position);
    applyCommonColumns(*e, rec);
    return e;
}

} // namespace

EmployeeRepository::EmployeeRepository() : Repository(QStringLiteral("employees")) {}

int EmployeeRepository::insert(const models::Employee& e) {
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("INSERT INTO employees "
                       "(name, phone, email, position, salary_paisa, shift, hired_date, "
                       " is_active, performance_rating) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"),
        { e.name(), e.phone(), e.email(), e.position(),
          static_cast<qlonglong>(e.salary().paisa()), e.shift(), dateOrNull(e.hiredDate()),
          e.isActive() ? 1 : 0, e.performanceRating() });
    return q.lastInsertId().toInt();
}

void EmployeeRepository::update(const models::Employee& e) {
    // `e` may well be a Waiter or an Admin arriving through an Employee& — only the base-class state
    // is persisted, because the role-specific extras (tips, overtime, bonus) are per-month figures
    // that the schema deliberately does not store.
    Database::instance().prepared(
        QStringLiteral("UPDATE employees SET name = ?, phone = ?, email = ?, position = ?, "
                       "salary_paisa = ?, shift = ?, hired_date = ?, is_active = ?, "
                       "performance_rating = ? WHERE id = ?"),
        { e.name(), e.phone(), e.email(), e.position(),
          static_cast<qlonglong>(e.salary().paisa()), e.shift(), dateOrNull(e.hiredDate()),
          e.isActive() ? 1 : 0, e.performanceRating(), e.id() });
}

std::unique_ptr<models::Employee> EmployeeRepository::makeTyped(int employeeId) const {
    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT * FROM employees WHERE id = ? LIMIT 1"), { employeeId });
    if (!q.next()) return nullptr;   // absent is not an error — the caller asked, and the answer is "no"
    return buildTyped(q.record());
}

std::vector<std::unique_ptr<models::Employee>> EmployeeRepository::allTyped() const {
    /// @oop-concept Object Pointers :: a heterogeneous staff list is a vector of base pointers
    std::vector<std::unique_ptr<models::Employee>> out;
    QSqlQuery q = Database::instance().exec(
        QStringLiteral("SELECT * FROM employees ORDER BY name"));
    while (q.next()) out.push_back(buildTyped(q.record()));
    return out;
}

void EmployeeRepository::markAttendance(int employeeId, QDate day, const QString& status,
                                        QTime checkIn, QTime checkOut) {
    // UPSERT on the (employee_id, work_date) unique key: marking somebody present twice corrects the
    // day rather than creating a second, contradictory row.
    Database::instance().prepared(
        QStringLiteral("INSERT INTO attendance (employee_id, work_date, check_in, check_out, status) "
                       "VALUES (?, ?, ?, ?, ?) "
                       "ON CONFLICT(employee_id, work_date) DO UPDATE SET "
                       "check_in = excluded.check_in, check_out = excluded.check_out, "
                       "status = excluded.status"),
        { employeeId, day.toString(QString::fromLatin1(kDateFormat)),
          timeOrNull(checkIn), timeOrNull(checkOut), status });
}

std::vector<std::tuple<QDate, QString, QTime, QTime>>
EmployeeRepository::attendanceFor(int employeeId, int year, int month) const {
    std::vector<std::tuple<QDate, QString, QTime, QTime>> out;

    const QDate monthStart(year, month, 1);
    if (!monthStart.isValid()) return out;
    const QDate monthEnd = monthStart.addMonths(1);   // half-open upper bound

    QSqlQuery q = Database::instance().prepared(
        QStringLiteral("SELECT work_date, status, check_in, check_out FROM attendance "
                       "WHERE employee_id = ? AND work_date >= ? AND work_date < ? "
                       "ORDER BY work_date"),
        { employeeId,
          monthStart.toString(QString::fromLatin1(kDateFormat)),
          monthEnd.toString(QString::fromLatin1(kDateFormat)) });

    while (q.next()) {
        out.emplace_back(QDate::fromString(q.value(0).toString(), QString::fromLatin1(kDateFormat)),
                         q.value(1).toString(),
                         timeFromDb(q.value(2)),
                         timeFromDb(q.value(3)));
    }
    return out;
}

models::Employee EmployeeRepository::fromRecord(const QSqlRecord& rec) const {
    // Deliberately base-sliced: list views and edit forms want a copyable value, not ownership of a
    // polymorphic object. Behaviour that differs per role is reached through makeTyped() instead.
    models::Employee e(rec.value(QStringLiteral("id")).toInt(),
                       rec.value(QStringLiteral("name")).toString(),
                       rec.value(QStringLiteral("phone")).toString(),
                       rec.value(QStringLiteral("email")).toString(),
                       rec.value(QStringLiteral("position")).toString(),
                       core::Money(rec.value(QStringLiteral("salary_paisa")).toLongLong()),
                       rec.value(QStringLiteral("shift")).toString());
    applyCommonColumns(e, rec);
    return e;
}

QString EmployeeRepository::orderByClause() const { return QStringLiteral("name"); }

} // namespace aluchop::persistence

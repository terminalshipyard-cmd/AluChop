/**
 * @file EmployeeService.cpp
 * @brief Staff records, attendance, payroll and the staff-customer fusion used by billing.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Two OOP mechanisms carry real weight in this file and neither is decoration.
 *
 * **Runtime polymorphism** drives payroll. `EmployeeRepository::allTyped()` reads the `position`
 * column and builds the matching concrete role on the heap — Waiter, Chef, Manager or Admin — and
 * `payrollPreview()` then simply calls `monthlyPay()` through a `std::unique_ptr<Employee>`. A
 * waiter's tips, a chef's overtime and a manager's bonus are each added by that role's own
 * override, so this service contains no `if (position == "CHEF")` chain at all. Adding a fifth
 * role would not change one line here.
 *
 * **The virtual-base diamond** is resolved by staffCustomerFor(). Staff eat here, get a staff
 * discount and still accrue loyalty points, which makes them simultaneously an Employee and a
 * Customer. Because both classes derive `virtual public Person`, the fused `StaffCustomer` holds
 * exactly one id, one name, one phone and one e-mail. Without the `virtual` keyword on both edges
 * the object would carry two Person subobjects, `sc.name()` would not even compile, and the two
 * halves could drift apart.
 */

#include "aluchop/services/EmployeeService.hpp"

#include <algorithm>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/services/AuditService.hpp"
#include "aluchop/services/NotificationService.hpp"

namespace aluchop::services {

namespace {

/// @brief The `NotificationService` domain name every staff mutation announces.
const QString kDomain = QStringLiteral("employees");

/// @brief The four position tokens the `employees.position` CHECK constraint accepts.
/// @oop-concept Object Arrays :: the legal positions as one const array, checked in one place
const QString kPositions[] = {
    QStringLiteral("WAITER"), QStringLiteral("CHEF"),
    QStringLiteral("MANAGER"), QStringLiteral("ADMIN")
};

/// @brief The three attendance status tokens the `attendance.status` CHECK constraint accepts.
const QString kAttendanceStatuses[] = {
    QStringLiteral("PRESENT"), QStringLiteral("ABSENT"), QStringLiteral("LEAVE")
};

/// @brief `"employee:<id>"` — the audit entity string for a staff member.
QString employeeEntity(int id) {
    return QStringLiteral("employee:%1").arg(id);
}

/// @brief Whether @p token appears in @p allowed.
template <std::size_t N>
bool isOneOf(const QString (&allowed)[N], const QString& token) {
    for (const QString& candidate : allowed)
        if (candidate == token) return true;
    return false;
}

/**
 * @brief Digits-only view of a phone number.
 *
 * The staff and loyalty rows are typed by different people at different times, so "+977 9812345678"
 * and "9812345678" must match. Comparing digits is what makes the diamond fusion actually fire in
 * practice instead of only in theory.
 */
QString normalisedPhone(const QString& raw) {
    QString digits;
    digits.reserve(raw.size());
    for (const QChar c : raw)
        if (c.isDigit()) digits.append(c);
    return digits;
}

} // namespace

EmployeeService::EmployeeService(persistence::EmployeeRepository& employees,
                                 persistence::CustomerRepository& customers,
                                 AuditService& audit, NotificationService& notify)
    : m_employees(employees), m_customers(customers), m_audit(audit), m_notify(notify) {
    // The customer repository is here for exactly one reason: staffCustomerFor() has to read both
    // sides of the diamond before it can fuse them.
}

// ---------------------------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------------------------

/// @oop-concept Object Pointers / Dynamic Memory Allocation :: the concrete role is a runtime fact
/// read out of a database column, so the objects must live on the heap behind unique_ptr
std::vector<std::unique_ptr<models::Employee>> EmployeeService::staff() const {
    try {
        return m_employees.allTyped();
    } catch (const core::AluChopException&) {
        return {};
    }
}

std::optional<models::Employee> EmployeeService::byId(int id) const {
    try {
        // A deliberately base-sliced Employee: an edit form needs the columns, not the behaviour.
        return m_employees.findById(id);
    } catch (const core::AluChopException&) {
        return std::nullopt;
    }
}

std::vector<std::tuple<QDate, QString, QTime, QTime>>
EmployeeService::attendanceFor(int employeeId, int year, int month) const {
    if (employeeId <= 0 || month < 1 || month > 12) return {};
    try {
        return m_employees.attendanceFor(employeeId, year, month);
    } catch (const core::AluChopException&) {
        return {};
    }
}

std::vector<std::tuple<QString, QString, core::Money>> EmployeeService::payrollPreview() const {
    std::vector<std::tuple<QString, QString, core::Money>> preview;

    std::vector<std::unique_ptr<models::Employee>> roster;
    try {
        roster = m_employees.allTyped();
    } catch (const core::AluChopException&) {
        return preview;
    }

    preview.reserve(roster.size());
    for (const std::unique_ptr<models::Employee>& person : roster) {
        if (!person || !person->isActive()) continue;   // leavers keep their history, not their pay

        /// @oop-concept Runtime Polymorphism :: all three columns come out of virtual dispatch —
        /// displayLabel() and roleName() from Person, monthlyPay() from the concrete role. This
        /// loop never asks what it is holding, which is the entire point of the hierarchy.
        preview.emplace_back(person->displayLabel(), person->roleName(), person->monthlyPay());
    }

    // Highest earner first — the reading order a payroll sheet is checked in.
    std::sort(preview.begin(), preview.end(),
              [](const std::tuple<QString, QString, core::Money>& a,
                 const std::tuple<QString, QString, core::Money>& b) {
                  if (std::get<2>(a) != std::get<2>(b)) return std::get<2>(b) < std::get<2>(a);
                  return std::get<0>(a).localeAwareCompare(std::get<0>(b)) < 0;
              });
    return preview;
}

// ---------------------------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------------------------

core::Result<int> EmployeeService::hire(const QString& name, const QString& phone,
                                        const QString& email, const QString& position,
                                        core::Money salary, const QString& shift) {
    const QString token = position.trimmed().toUpper();
    try {
        if (!isOneOf(kPositions, token))
            throw core::ValidationException(
                "the position must be WAITER, CHEF, MANAGER or ADMIN", "position");
        if (salary.isNegative())
            throw core::ValidationException("a salary cannot be negative", "salary");

        // Every remaining rule (blank name, malformed phone, malformed e-mail, blank shift) is
        // enforced by the Employee constructor's setters, which throw a ValidationException naming
        // the field — so the GUI can highlight exactly one input.
        models::Employee fresh(0, name.trimmed(), phone.trimmed(), email.trimmed(),
                               token, salary, shift.trimmed().isEmpty()
                                                  ? QStringLiteral("DAY")
                                                  : shift.trimmed().toUpper());
        fresh.setHiredDate(QDate::currentDate());
        fresh.setActive(true);

        const int newId = m_employees.insert(fresh);

        m_audit.log(QStringLiteral("EMP_HIRE"), employeeEntity(newId), salary,
                    QStringLiteral("%1 as %2").arg(fresh.name(), token));
        m_notify.notify(QStringLiteral("Staff hired"),
                        QStringLiteral("%1 joined as %2.").arg(fresh.name(), token.toLower()),
                        static_cast<int>(models::NoticeLevel::Success));
        m_notify.announceDataChanged(kDomain);
        return core::Result<int>::ok(newId);
    } catch (const core::AluChopException& e) {
        return core::Result<int>::err(QString::fromStdString(e.message()));
    }
}

core::Result<void> EmployeeService::update(const models::Employee& e) {
    try {
        if (e.id() <= 0)
            throw core::ValidationException("that staff member has not been saved yet", "id");
        if (!m_employees.findById(e.id()))
            throw core::ValidationException("that staff member no longer exists", "id");

        m_employees.update(e);

        m_audit.log(QStringLiteral("EMP_UPDATE"), employeeEntity(e.id()), e.salary(), e.name());
        m_notify.announceDataChanged(kDomain);
        return core::Result<void>::ok();
    } catch (const core::AluChopException& ex) {
        return core::Result<void>::err(QString::fromStdString(ex.message()));
    }
}

core::Result<void> EmployeeService::deactivate(int employeeId) {
    try {
        std::optional<models::Employee> person = m_employees.findById(employeeId);
        if (!person)
            throw core::ValidationException("that staff member no longer exists", "employeeId");
        if (!person->isActive())
            return core::Result<void>::ok();   // already off the roster

        // A soft delete, never a real one: payroll history, past orders and the attendance record
        // all reference this row and must keep resolving.
        person->setActive(false);
        m_employees.update(*person);

        m_audit.log(QStringLiteral("EMP_DEACT"), employeeEntity(employeeId), core::Money(),
                    person->name());
        m_notify.notify(QStringLiteral("Staff deactivated"),
                        QStringLiteral("%1 is no longer on the active roster.").arg(person->name()),
                        static_cast<int>(models::NoticeLevel::Warning));
        m_notify.announceDataChanged(kDomain);
        return core::Result<void>::ok();
    } catch (const core::AluChopException& e) {
        return core::Result<void>::err(QString::fromStdString(e.message()));
    }
}

core::Result<void> EmployeeService::markAttendance(int employeeId, QDate day, const QString& status,
                                                   QTime checkIn, QTime checkOut) {
    const QString token = status.trimmed().toUpper();
    try {
        if (!m_employees.findById(employeeId))
            throw core::ValidationException("that staff member no longer exists", "employeeId");
        if (!day.isValid())
            throw core::ValidationException("the attendance date is not a real date", "day");
        if (day > QDate::currentDate())
            throw core::ValidationException("attendance cannot be recorded for a future day", "day");
        if (!isOneOf(kAttendanceStatuses, token))
            throw core::ValidationException("the status must be PRESENT, ABSENT or LEAVE", "status");

        // ABSENT and LEAVE days carry no clock times; storing stale ones would make the month view
        // read as though somebody worked a shift they did not.
        QTime in = checkIn;
        QTime out = checkOut;
        if (token != QStringLiteral("PRESENT")) {
            in = QTime();
            out = QTime();
        } else if (in.isValid() && out.isValid() && out < in) {
            throw core::ValidationException("check-out is earlier than check-in", "checkOut");
        }

        m_employees.markAttendance(employeeId, day, token, in, out);

        m_audit.log(QStringLiteral("EMP_ATTEND"), employeeEntity(employeeId), core::Money(),
                    QStringLiteral("%1 %2").arg(day.toString(QStringLiteral("yyyy-MM-dd")), token));
        m_notify.announceDataChanged(kDomain);
        return core::Result<void>::ok();
    } catch (const core::AluChopException& e) {
        return core::Result<void>::err(QString::fromStdString(e.message()));
    }
}

// ---------------------------------------------------------------------------------------------
// The diamond
// ---------------------------------------------------------------------------------------------

std::optional<models::StaffCustomer> EmployeeService::staffCustomerFor(int customerId) const {
    try {
        const std::optional<models::Customer> guest = m_customers.findById(customerId);
        if (!guest) return std::nullopt;

        const QString wanted = normalisedPhone(guest->phone());
        if (wanted.isEmpty()) return std::nullopt;   // nothing to match a staff row on

        // Base-sliced rows are enough here: the fusion needs the employment *columns*, and the
        // polymorphic pay rule plays no part in a discount.
        const std::vector<models::Employee> roster = m_employees.findAll();
        const auto hit = std::find_if(roster.begin(), roster.end(),
                                      [&wanted](const models::Employee& e) {
                                          return e.isActive() && normalisedPhone(e.phone()) == wanted;
                                      });
        if (hit == roster.end()) return std::nullopt;   // an ordinary guest, not a colleague

        /// @oop-concept Virtual Base Class :: this single constructor call initialises the ONE
        /// shared Person subobject. Because Employee and Customer both derive Person virtually,
        /// the most-derived class — StaffCustomer — is what initialises it, and the object that
        /// comes out has one id, one name, one phone and one e-mail rather than two of each.
        models::StaffCustomer fused(guest->id(), guest->name(), guest->phone(), guest->email(),
                                    hit->position(), hit->salary(), hit->shift());

        // Employment attributes come from the staff row...
        fused.setHiredDate(hit->hiredDate());
        fused.setPerformanceRating(hit->performanceRating());
        fused.setActive(hit->isActive());

        // ...and the loyalty attributes from the guest row. Both sets now hang off one identity,
        // which is precisely what lets BillingService ask a single object both
        // "what is your staff discount?" and "how many points do you have?".
        fused.setLoyaltyPoints(guest->loyaltyPoints());
        fused.setVisits(guest->visits());
        fused.setCreatedAt(guest->createdAt());

        return fused;
    } catch (const core::AluChopException&) {
        // Billing must still be able to take money when this lookup cannot be completed; a missing
        // fusion simply means no staff discount is offered.
        return std::nullopt;
    }
}

} // namespace aluchop::services

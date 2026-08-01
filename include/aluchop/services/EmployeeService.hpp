#pragma once

/**
 * @file EmployeeService.hpp
 * @brief Staff records, attendance and payroll.
 *
 * Payroll is the clearest piece of runtime polymorphism in the project: the service holds
 * `std::unique_ptr<models::Employee>` handles built from the `position` column and simply calls
 * `monthlyPay()`. A waiter adds tips, a chef adds overtime, a manager adds a bonus — the service
 * itself contains no `if (position == ...)` chain at all.
 */

#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include <QDate>
#include <QString>
#include <QTime>

#include "aluchop/core/Money.hpp"
#include "aluchop/core/Result.hpp"
#include "aluchop/models/Employee.hpp"
#include "aluchop/models/StaffCustomer.hpp"
#include "aluchop/persistence/CustomerRepository.hpp"
#include "aluchop/persistence/EmployeeRepository.hpp"

namespace aluchop::services {

class AuditService;
class NotificationService;

/// @brief Hiring, editing, attendance, payroll and the staff-customer fusion used by billing.
class EmployeeService {
public:
    EmployeeService(persistence::EmployeeRepository& employees,
                    persistence::CustomerRepository& customers,
                    AuditService& audit, NotificationService& notify);

    /**
     * @brief The staff list as concrete role objects.
     * /// @oop-concept Object Pointers / Runtime Polymorphism :: payroll iterates
     * /// unique_ptr<Employee> and calls monthlyPay() virtually — Waiter/Chef/Manager/Admin each
     * /// compute differently, and this service never asks which one it is holding
     * /// @oop-concept Dynamic Memory Allocation :: the concrete type is a runtime fact, so the
     * /// objects must live on the heap, owned by unique_ptr
     */
    std::vector<std::unique_ptr<models::Employee>> staff() const;

    /// @return a base-sliced employee, which is all an edit form needs.
    std::optional<models::Employee> byId(int id) const;

    /**
     * @brief Hires a staff member.
     * @param position one of `WAITER`, `CHEF`, `MANAGER`, `ADMIN` — it decides the runtime role.
     * @return the new employee id, or a validation error.
     */
    core::Result<int> hire(const QString& name, const QString& phone, const QString& email,
                           const QString& position, core::Money salary, const QString& shift);

    /// @brief Saves edited staff details.
    core::Result<void> update(const models::Employee& e);

    /// @brief Marks a staff member inactive; records are kept for payroll history.
    core::Result<void> deactivate(int employeeId);

    /// @brief Records attendance for one day.
    /// @oop-concept Default Arguments :: ABSENT and LEAVE days carry no clock times
    core::Result<void> markAttendance(int employeeId, QDate day, const QString& status,
                                      QTime checkIn = QTime(), QTime checkOut = QTime());

    /// @return (work date, status, check-in, check-out) for one employee in one month.
    std::vector<std::tuple<QDate, QString, QTime, QTime>>
        attendanceFor(int employeeId, int year, int month) const;

    /**
     * @brief Payroll preview for every active staff member.
     * @return (display label, role name, monthly pay) — all three produced by virtual dispatch.
     */
    std::vector<std::tuple<QString, QString, core::Money>> payrollPreview() const;

    /**
     * @brief The diamond in action: a customer who is also an active employee.
     * @return a fused `StaffCustomer` (one `Person` identity, both roles) when this customer's
     *         phone matches active staff — `BillingService` uses it for the staff discount.
     *
     * /// @oop-concept Virtual Base Class :: because Employee and Customer both derive Person
     * /// virtually, the returned object has ONE id, ONE name and one unambiguous name() —
     * /// exactly the reason the diamond is solved virtually rather than duplicated
     */
    std::optional<models::StaffCustomer> staffCustomerFor(int customerId) const;

private:
    persistence::EmployeeRepository& m_employees;
    persistence::CustomerRepository& m_customers;
    AuditService& m_audit;
    NotificationService& m_notify;
};

} // namespace aluchop::services

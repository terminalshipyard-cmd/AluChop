#pragma once

/**
 * @file EmployeesPage.hpp
 * @brief Screen 5 — staff records, attendance and payroll preview.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QString>

#include "aluchop/gui/Page.hpp"

class QTableWidget;

namespace aluchop::gui {

/**
 * @brief Staff administration: who works here, when they showed up, what they are owed.
 *
 * Uses services::EmployeeService exclusively. The service hands back
 * `std::vector<std::unique_ptr<models::Employee>>` — a genuinely polymorphic collection of
 * Waiter, Chef, Manager and Admin objects — and this screen prints each one's
 * `roleName()` and `monthlyPay()` without ever asking what concrete type it holds. That is
 * the payroll preview: one loop, four different pay formulas.
 *
 * Attendance marking and shift/position edits are ordinary service calls; the month view is a
 * read-only projection the service builds.
 *
 * @oop-concept Runtime Polymorphism (surfaced) :: the Payroll column is
 *              models::Employee::monthlyPay() dispatched through a base-class pointer
 * @oop-concept Object Pointers :: staff arrive as unique_ptr<Employee>, the only ownership in
 *              this layer that is not Qt's
 */
class EmployeesPage final : public Page {
    Q_OBJECT
public:
    /// @param ctx composition root; uses AppContext::employees() and auth() for role gating.
    /// @param parent Qt parent.
    explicit EmployeesPage(services::AppContext& ctx, QWidget* parent = nullptr);

    /// @return "Employees".
    QString pageTitle() const override;

    /// Reloads the staff list, this month's attendance and the payroll preview.
    void refresh() override;

private slots:
    /// Hires a new staff member; the role picker chooses which concrete Employee subclass the
    /// service factory allocates.
    void onHire();

    /// Edits the selected staff member's details, salary, shift or position.
    void onEdit();

    /// Deactivates the selected staff member (records are kept for reports and audit).
    void onDeactivate();

    /// Marks the selected staff member present/absent for a chosen date.
    void onMarkAttendance();

    /// Recomputes and shows the payroll preview for the selected month.
    void onShowPayroll();

private:
    /// @return the id of the selected employee, or 0 when nothing is selected.
    int selectedEmployeeId() const;

    QTableWidget* m_table = nullptr;      ///< Name | Role | Position | Shift | Salary | Status.
    QTableWidget* m_attendance = nullptr; ///< Day-by-day presence for the selected month.
    QTableWidget* m_payroll = nullptr;    ///< Name | Role | Base | Extras | Monthly pay.
};

} // namespace aluchop::gui

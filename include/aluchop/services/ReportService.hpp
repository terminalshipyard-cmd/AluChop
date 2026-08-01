#pragma once

/**
 * @file ReportService.hpp
 * @brief Dashboard analytics and the report factory.
 *
 * Two jobs live here: the small aggregates the dashboard paints (today's sales, the seven-day
 * revenue bar chart, popular dishes, customer and pending-order counts), and the factory that
 * hands out a concrete `ReportGenerator` for a chosen kind and date range.
 *
 * Every money figure is `core::Money` (integer paisa) end to end — no revenue number is ever a
 * `double`, not even on the way to a chart.
 */

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include <QDate>
#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/services/ReportGenerator.hpp"

namespace aluchop::services {

/// @brief The five reports the Reports screen offers.
/// @oop-concept Enumerations :: the report kinds are a closed set, chosen in a combo box
enum class ReportKind { Sales, Inventory, Orders, Customers, Employees };

/// @brief Read-only analytics over the payment, order, customer, employee and stock tables.
class ReportService {
public:
    ReportService(const persistence::PaymentRepository& payments,
                  const persistence::OrderRepository& orders,
                  const persistence::CustomerRepository& customers,
                  const persistence::EmployeeRepository& employees,
                  const persistence::IngredientRepository& ingredients);

    /// @return total settled revenue for one calendar day.
    core::Money salesForDay(QDate day) const;

    /**
     * @brief Seven daily totals ending on @p weekEnding.
     * @return `[0]` is six days before @p weekEnding, `[6]` is @p weekEnding itself.
     * /// @oop-concept Object Arrays :: seven Money objects, one per day, drive the weekly chart —
     * /// a fixed-size std::array says "exactly a week" in the type itself
     */
    std::array<core::Money, 7> weeklySales(QDate weekEnding) const;

    /// @return total settled revenue for one calendar month.
    core::Money salesForMonth(int year, int month) const;

    /// @brief Best-selling dishes of the last 30 days, as (name, quantity).
    /// @oop-concept Default Arguments :: the dashboard card shows a top five
    std::vector<std::pair<QString, int>> popularItems(int topN = 5) const;

    /// @return one (date, revenue) point per day across the range — the line/bar chart series.
    std::vector<std::pair<QDate, core::Money>> revenueSeries(QDate from, QDate to) const;

    /// @return how many customers are registered.
    int customerCount() const;

    /// @return orders currently Pending, Preparing or Ready — the kitchen's outstanding workload.
    int pendingOrderCount() const;

    /**
     * @brief Builds the requested report.
     * @return a heap-allocated concrete report owned by the caller.
     * /// @oop-concept Dynamic Objects :: the concrete report type is a runtime choice, so it is
     * /// created with std::make_unique and returned through the abstract base
     */
    std::unique_ptr<ReportGenerator> makeReport(ReportKind kind, QDate from, QDate to) const;

private:
    const persistence::PaymentRepository& m_payments;
    const persistence::OrderRepository& m_orders;
    const persistence::CustomerRepository& m_customers;
    const persistence::EmployeeRepository& m_employees;
    const persistence::IngredientRepository& m_ingredients;
};

} // namespace aluchop::services

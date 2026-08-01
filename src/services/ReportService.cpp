/**
 * @file ReportService.cpp
 * @brief Implementation of the dashboard aggregates and the report factory.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Two jobs live here: the small aggregates the dashboard paints, and the factory that hands out a
 * concrete `ReportGenerator` for a chosen kind and date range.
 *
 * @warning Every money figure is `core::Money` (integer paisa) end to end. Not one revenue number
 *          in this file is ever a `double`, not even on the way to a chart — QtCharts is fed from
 *          `Money::paisa()` at the presentation edge, never from a floating-point accumulator.
 *          Revenue is read from the `payments` ledger, which stores what the guest actually paid;
 *          it is never re-derived from menu prices, and no tax is added to anything.
 */

#include "aluchop/services/ReportService.hpp"

#include <utility>
#include <vector>

#include <QDateTime>
#include <QTimeZone>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Logger.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/Order.hpp"

namespace aluchop::services {

namespace {

/// Timestamps are stored ISO-8601 UTC (docs/ARCHITECTURE.md §6), so windows are built in UTC.
QDateTime dayStartUtc(QDate d) {
    return d.startOfDay(QTimeZone::UTC);
}

/// A chart with more points than this is unreadable anyway, and a mis-set picker must not stall
/// the GUI thread with thousands of queries.
constexpr int kMaxSeriesDays = 366;

/// Popular dishes are measured over the trailing month.
constexpr int kPopularWindowDays = 30;

QString exceptionText(const core::AluChopException& ex) {
    return QString::fromUtf8(ex.what());
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

/// @oop-concept Constant Objects :: every repository is held as a `const&` — this service reads
/// the business and is structurally incapable of changing it.
ReportService::ReportService(const persistence::PaymentRepository& payments,
                             const persistence::OrderRepository& orders,
                             const persistence::CustomerRepository& customers,
                             const persistence::EmployeeRepository& employees,
                             const persistence::IngredientRepository& ingredients)
    : m_payments(payments),
      m_orders(orders),
      m_customers(customers),
      m_employees(employees),
      m_ingredients(ingredients) {}

// ---------------------------------------------------------------------------
// Revenue aggregates
// ---------------------------------------------------------------------------

core::Money ReportService::salesForDay(QDate day) const {
    if (!day.isValid())
        return core::Money::zero();
    try {
        return m_payments.revenueBetween(dayStartUtc(day), dayStartUtc(day.addDays(1)));
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(QStringLiteral("sales for %1 could not be read: %2")
                                           .arg(day.toString(Qt::ISODate), exceptionText(ex)));
        return core::Money::zero();
    }
}

/// @oop-concept Object Arrays :: seven Money objects, one per day, drive the weekly chart —
/// a fixed-size std::array says "exactly a week" in the type itself, so a caller cannot hand the
/// chart six days or eight.
std::array<core::Money, 7> ReportService::weeklySales(QDate weekEnding) const {
    std::array<core::Money, 7> week{};    // value-initialised: seven Rs 0.00 objects
    if (!weekEnding.isValid())
        return week;

    for (int i = 0; i < 7; ++i)
        week[static_cast<std::size_t>(i)] = salesForDay(weekEnding.addDays(i - 6));

    return week;                          // [0] = six days before, [6] = weekEnding itself
}

core::Money ReportService::salesForMonth(int year, int month) const {
    if (month < 1 || month > 12)
        return core::Money::zero();

    const QDate first(year, month, 1);
    if (!first.isValid())
        return core::Money::zero();

    try {
        return m_payments.revenueBetween(dayStartUtc(first), dayStartUtc(first.addMonths(1)));
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("sales for %1-%2 could not be read: %3")
                .arg(year).arg(month).arg(exceptionText(ex)));
        return core::Money::zero();
    }
}

std::vector<std::pair<QString, int>> ReportService::popularItems(int topN) const {
    if (topN < 1)
        return {};

    const QDateTime to = QDateTime::currentDateTimeUtc();
    const QDateTime from = to.addDays(-kPopularWindowDays);

    try {
        return m_payments.popularItems(from, to, topN);
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("popular items could not be read: %1").arg(exceptionText(ex)));
        return {};
    }
}

std::vector<std::pair<QDate, core::Money>> ReportService::revenueSeries(QDate from, QDate to) const {
    std::vector<std::pair<QDate, core::Money>> series;
    if (!from.isValid() || !to.isValid() || from > to)
        return series;

    const int days = static_cast<int>(from.daysTo(to)) + 1;
    const int span = days > kMaxSeriesDays ? kMaxSeriesDays : days;

    series.reserve(static_cast<std::size_t>(span));
    for (int i = 0; i < span; ++i) {
        const QDate day = from.addDays(i);
        series.emplace_back(day, salesForDay(day));
    }
    return series;
}

// ---------------------------------------------------------------------------
// Counts
// ---------------------------------------------------------------------------

int ReportService::customerCount() const {
    try {
        return m_customers.count();
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("customer count could not be read: %1").arg(exceptionText(ex)));
        return 0;
    }
}

int ReportService::pendingOrderCount() const {
    // "Outstanding workload" is everything the kitchen still owes the floor: fired but not started,
    // cooking, and plated but not yet carried out.
    static const models::OrderStatus kOutstanding[] = {models::OrderStatus::Pending,
                                                       models::OrderStatus::Preparing,
                                                       models::OrderStatus::Ready};
    int n = 0;
    try {
        for (const models::OrderStatus s : kOutstanding)
            n += static_cast<int>(m_orders.withStatus(s).size());
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("pending order count could not be read: %1").arg(exceptionText(ex)));
        return n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// The report factory
// ---------------------------------------------------------------------------

/// @oop-concept Dynamic Objects :: the concrete report type is a runtime choice made in a combo
/// box, so the object must be created on the heap with std::make_unique and handed back through
/// the abstract base — the caller then drives it purely through ReportGenerator's interface.
std::unique_ptr<ReportGenerator> ReportService::makeReport(ReportKind kind, QDate from,
                                                           QDate to) const {
    switch (kind) {
    case ReportKind::Sales:
        return std::make_unique<SalesReport>(m_payments, from, to);
    case ReportKind::Inventory:
        return std::make_unique<InventoryReport>(m_ingredients);
    case ReportKind::Orders:
        return std::make_unique<OrdersReport>(m_orders, from, to);
    case ReportKind::Customers:
        return std::make_unique<CustomersReport>(m_customers);
    case ReportKind::Employees:
        return std::make_unique<EmployeesReport>(m_employees);
    }

    // Unreachable while ReportKind has exactly the five enumerators above; if a sixth is ever
    // added without a case here, this throws loudly rather than silently returning nullptr.
    throw core::ValidationException("unknown report kind", "ReportKind",
                                    static_cast<int>(kind));
}

} // namespace aluchop::services

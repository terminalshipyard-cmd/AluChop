/**
 * @file ReportService.cpp
 * @brief Implementation of the dashboard aggregates and the report factory.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Two jobs live here: the small aggregates the dashboard paints, and the factory that hands out a
 * concrete `ReportGenerator` for a chosen kind and date range.
 *
 * @note **Every `QDate` crossing this interface is a LOCAL calendar date.** A business day opens at
 *       local midnight and closes at the next one, because that is the day the restaurant and its
 *       guests live in; see `dayStartLocal()` below for why a UTC-windowed "day" quietly misfiles a
 *       Nepali restaurant's early-morning takings. Callers pass `QDate::currentDate()` (or a
 *       `QDateEdit` value) — never `QDateTime::currentDateTimeUtc().date()`.
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

/**
 * @brief Local midnight at the start of @p d — the moment that business day opens.
 *
 * **Windowing contract (binding for every caller of this service):** every `QDate` handed to
 * `ReportService` is a **LOCAL calendar date** — exactly what `QDate::currentDate()` and a
 * `QDateEdit` produce — and a business day runs from local midnight to the next local midnight.
 *
 * Timestamps are *stored* ISO-8601 UTC (docs/ARCHITECTURE.md §6), but storage format and business
 * meaning are different things: the instants below are converted to UTC inside the repository, on
 * the way into SQL. Windowing the *day* in UTC as well would be wrong for a restaurant in Nepal
 * (UTC+05:45), where a UTC day runs 05:45 → 05:45 local: every sale rung up before a quarter to six
 * in the morning — the tail of the previous night's service — would be filed under the day before,
 * and "today's sales" on the dashboard would silently disagree with the date printed beside it.
 * The rule that makes the figures defensible is the one a restaurateur already uses: the takings
 * belong to the date that was on the wall when the guest paid.
 */
QDateTime dayStartLocal(QDate d) {
    return d.startOfDay(QTimeZone::LocalTime);
}

/**
 * @brief The exclusive upper bound that still includes everything settled up to and including @p at.
 *
 * `payments.paid_at` is stored truncated to whole seconds, so a bill settled at 12:00:00.400 sits on
 * disk as `12:00:00`. Asking for `paid_at < now` with `now = 12:00:00.912` therefore cannot match it
 * — the bound truncates to the very same `12:00:00` — and the sale disappears from the report until
 * the clock ticks over. Rounding the bound up to the start of the next second keeps the window
 * half-open while covering the whole of the second we are currently living in, which is what "up to
 * now" was always meant to say. (The repository snaps mid-second bounds the same way; doing it here
 * as well is not belt-and-braces but intent: this is the call site that means *inclusive of now*,
 * and it stays correct even in the one run in a thousand where `now` lands exactly on a second.)
 */
QDateTime throughInstant(const QDateTime& at) {
    return at.addMSecs(1000 - at.time().msec());
}

/**
 * @brief Rejects a window that cannot be asked of the database.
 *
 * `QDate::startOfDay` returns an *invalid* QDateTime for a date a time zone skipped wholesale, and
 * an invalid bound formats to an empty string in SQL — where `paid_at >= ''` is true of every row
 * ever written. Refusing the window keeps a nonexistent day reading zero instead of "all revenue,
 * ever".
 */
bool windowIsSane(const QDateTime& from, const QDateTime& to) {
    return from.isValid() && to.isValid() && from < to;
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

/// @param day a LOCAL calendar date — the business day whose takings are wanted.
core::Money ReportService::salesForDay(QDate day) const {
    if (!day.isValid())
        return core::Money::zero();

    const QDateTime from = dayStartLocal(day);
    const QDateTime to = dayStartLocal(day.addDays(1));   // exclusive: the next day's opening moment
    if (!windowIsSane(from, to))
        return core::Money::zero();

    try {
        return m_payments.revenueBetween(from, to);
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

    // One window for the whole month rather than 28–31 day queries: the boundaries are the same
    // local midnights, so the figure is identical to summing the days, for a thirtieth of the work.
    const QDateTime from = dayStartLocal(first);
    const QDateTime to = dayStartLocal(first.addMonths(1));
    if (!windowIsSane(from, to))
        return core::Money::zero();

    try {
        return m_payments.revenueBetween(from, to);
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

    // A trailing 30 days ending *now* — and "now" has to mean now: this card is repainted the
    // instant a bill is settled, so the sale that was just rung up must already be in it.
    const QDateTime to = throughInstant(QDateTime::currentDateTimeUtc());
    const QDateTime from = to.addDays(-kPopularWindowDays);
    if (!windowIsSane(from, to))
        return {};

    try {
        return m_payments.popularItems(from, to, topN);
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("popular items could not be read: %1").arg(exceptionText(ex)));
        return {};
    }
}

/// @param from,to LOCAL calendar dates, inclusive at both ends — one chart point per business day.
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

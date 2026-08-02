/**
 * @file ReportGenerator.cpp
 * @brief Implementation of the five exportable reports and the CSV template method they share.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * `ReportGenerator::exportCsv` is a **template method**: the algorithm — open, write the header,
 * write every row, close — is written exactly once here, and the three things that vary between
 * reports (`title()`, `header()`, `rows()`) are pure virtual. Adding a sixth report therefore means
 * adding a class, and touching nothing in this function.
 *
 * The base derives **protected** from `persistence::CsvWriter`: a report is *implemented in terms
 * of* a writer rather than being one, so `exportCsv()` is callable from outside while `writeRow()`
 * is not — yet the five derived reports still inherit the writer verbs, which private inheritance
 * would have denied them.
 *
 * @warning Every money cell is rendered from `core::Money`. No report re-derives a total from a
 *          menu price, so a re-priced dish can never rewrite yesterday's figures, and no tax is
 *          ever added to a tax-inclusive amount.
 */

#include "aluchop/services/ReportGenerator.hpp"

#include <memory>
#include <vector>

#include <QDateTime>
#include <QTimeZone>

#include "aluchop/core/Algorithms.hpp"
#include "aluchop/core/Money.hpp"
#include "aluchop/models/Customer.hpp"
#include "aluchop/models/Employee.hpp"
#include "aluchop/models/Ingredient.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/models/Payment.hpp"

namespace aluchop::services {

namespace {

/**
 * @brief Local midnight at the start of @p d — the opening moment of that business day.
 *
 * This MUST match `ReportService`'s `dayStartLocal`, and for the same reason. Timestamps are
 * *stored* ISO-8601 UTC (docs/ARCHITECTURE.md §6), but storage format and business meaning are
 * different things: the repository converts these instants to UTC on the way into SQL.
 *
 * Windowing the *day* in UTC as well would be wrong for a restaurant in Nepal (UTC+05:45), where a
 * UTC day runs 05:45 → 05:45 local. It also silently disagreed with the dashboard: `ReportService`
 * windows on the local day, so a sale rung up after local midnight-minus-the-offset fell outside
 * the report's UTC window entirely and every report read zero while the dashboard read the true
 * takings. The takings belong to the date that was on the wall when the guest paid.
 */
QDateTime dayStartLocal(QDate d) {
    return d.startOfDay(QTimeZone::LocalTime);
}

/// @brief Human date for a report cell.
QString cellDate(QDate d) {
    return d.isValid() ? d.toString(Qt::ISODate) : QString();
}

/// @brief Human range for a report title.
QString rangeText(QDate from, QDate to) {
    return QStringLiteral("%1 to %2").arg(cellDate(from), cellDate(to));
}

/// @brief Guards a range so a mis-set date picker cannot ask for a million rows.
constexpr int kMaxRangeDays = 366;

} // namespace

// ---------------------------------------------------------------------------
// ReportGenerator — the shared algorithm
// ---------------------------------------------------------------------------

/// @oop-concept Runtime Polymorphism :: one algorithm here, five different files produced —
/// header() and rows() dispatch to whichever concrete report the caller happens to hold.
QString ReportGenerator::exportCsv(const QString& outPath) {
    // rows() is documented as re-querying, so it is called exactly once and kept.
    const std::vector<QStringList> body = rows();

    open(outPath);                       // protected CsvWriter::open — throws on failure

    try {
        writeRow(header());
        for (const QStringList& row : body)
            writeRow(row);
        close();
    } catch (...) {
        /// @oop-concept Rethrow :: a half-written export must not be left holding the file handle,
        /// but the caller still has to see the original failure — so the stream is released and
        /// the exception is re-thrown untouched with a bare `throw;`.
        try {
            close();
        } catch (...) {
            // Already failing; a second failure while closing must not mask the first.
        }
        throw;
    }

    return outPath;
}

// ---------------------------------------------------------------------------
// SalesReport — revenue per day
// ---------------------------------------------------------------------------

SalesReport::SalesReport(const persistence::PaymentRepository& payments, QDate from, QDate to)
    : m_payments(payments), m_from(from), m_to(to) {}

QString SalesReport::title() const {
    return QStringLiteral("Sales Report %1").arg(rangeText(m_from, m_to));
}

QStringList SalesReport::header() const {
    return QStringList{QStringLiteral("Date"), QStringLiteral("Orders Paid"),
                       QStringLiteral("Revenue")};
}

std::vector<QStringList> SalesReport::rows() const {
    std::vector<QStringList> out;
    if (!m_from.isValid() || !m_to.isValid() || m_from > m_to)
        return out;

    const int days = static_cast<int>(m_from.daysTo(m_to)) + 1;
    const int span = days > kMaxRangeDays ? kMaxRangeDays : days;
    out.reserve(static_cast<std::size_t>(span) + 1);

    core::Money grand;
    int grandCount = 0;

    for (int i = 0; i < span; ++i) {
        const QDate day = m_from.addDays(i);
        const std::vector<models::Payment> taken =
            m_payments.between(dayStartLocal(day), dayStartLocal(day.addDays(1)));

        /// @oop-concept Function Template :: core::sumMoney adds a projected Money field over any
        /// container — the same helper that totals an order's lines totals a day's payments.
        const core::Money revenue =
            core::sumMoney(taken, [](const models::Payment& p) { return p.total(); });

        grand += revenue;
        grandCount += static_cast<int>(taken.size());

        out.push_back(QStringList{cellDate(day),
                                  QString::number(static_cast<int>(taken.size())),
                                  revenue.toString()});
    }

    out.push_back(QStringList{QStringLiteral("TOTAL"), QString::number(grandCount),
                              grand.toString()});
    return out;
}

// ---------------------------------------------------------------------------
// InventoryReport — the current stock position
// ---------------------------------------------------------------------------

InventoryReport::InventoryReport(const persistence::IngredientRepository& ingredients)
    : m_ingredients(ingredients) {}

QString InventoryReport::title() const {
    return QStringLiteral("Inventory Report %1")
        .arg(QDate::currentDate().toString(Qt::ISODate));
}

QStringList InventoryReport::header() const {
    return QStringList{QStringLiteral("Ingredient"), QStringLiteral("Unit"),
                       QStringLiteral("Stock"),      QStringLiteral("Threshold"),
                       QStringLiteral("Low?"),       QStringLiteral("Expiry"),
                       QStringLiteral("Unit Cost")};
}

std::vector<QStringList> InventoryReport::rows() const {
    const std::vector<models::Ingredient> items = m_ingredients.findAll();

    std::vector<QStringList> out;
    out.reserve(items.size());

    for (const models::Ingredient& i : items) {
        out.push_back(QStringList{
            i.name(),
            i.unit(),
            QString::number(i.stockQty()),
            QString::number(i.lowThreshold()),
            i.isLow() ? QStringLiteral("LOW") : QStringLiteral(""),
            cellDate(i.expiryDate()),
            i.unitCost().toString()});
    }
    return out;
}

// ---------------------------------------------------------------------------
// OrdersReport — every order raised in the range
// ---------------------------------------------------------------------------

OrdersReport::OrdersReport(const persistence::OrderRepository& orders, QDate from, QDate to)
    : m_orders(orders), m_from(from), m_to(to) {}

QString OrdersReport::title() const {
    return QStringLiteral("Orders Report %1").arg(rangeText(m_from, m_to));
}

QStringList OrdersReport::header() const {
    return QStringList{QStringLiteral("Order #"),  QStringLiteral("Type"),
                       QStringLiteral("Status"),   QStringLiteral("Items"),
                       QStringLiteral("Subtotal"), QStringLiteral("Created")};
}

std::vector<QStringList> OrdersReport::rows() const {
    std::vector<QStringList> out;
    if (!m_from.isValid() || !m_to.isValid() || m_from > m_to)
        return out;

    QDate end = m_to;
    if (m_from.daysTo(end) + 1 > kMaxRangeDays)
        end = m_from.addDays(kMaxRangeDays - 1);

    const std::vector<models::Order> orders =
        m_orders.between(dayStartLocal(m_from), dayStartLocal(end.addDays(1)));

    out.reserve(orders.size());
    for (const models::Order& o : orders) {
        out.push_back(QStringList{
            o.orderNumber(),
            models::toString(o.type()),
            models::toString(o.status()),
            QString::number(static_cast<int>(o.itemCount())),
            o.subtotal().toString(),
            o.createdAt().toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))});
    }
    return out;
}

// ---------------------------------------------------------------------------
// CustomersReport — the loyalty database
// ---------------------------------------------------------------------------

CustomersReport::CustomersReport(const persistence::CustomerRepository& customers)
    : m_customers(customers) {}

QString CustomersReport::title() const {
    return QStringLiteral("Customers Report %1")
        .arg(QDate::currentDate().toString(Qt::ISODate));
}

QStringList CustomersReport::header() const {
    return QStringList{QStringLiteral("Name"), QStringLiteral("Phone"), QStringLiteral("Email"),
                       QStringLiteral("Visits"), QStringLiteral("Points")};
}

std::vector<QStringList> CustomersReport::rows() const {
    const std::vector<models::Customer> people = m_customers.findAll();

    std::vector<QStringList> out;
    out.reserve(people.size());

    for (const models::Customer& c : people) {
        // name()/phone()/email() come from the single virtual Person base, which is why one
        // accessor works for a plain Customer and for a StaffCustomer alike.
        out.push_back(QStringList{c.name(), c.phone(), c.email(),
                                  QString::number(c.visits()),
                                  QString::number(c.loyaltyPoints())});
    }
    return out;
}

// ---------------------------------------------------------------------------
// EmployeesReport — the roster with computed pay
// ---------------------------------------------------------------------------

EmployeesReport::EmployeesReport(const persistence::EmployeeRepository& employees)
    : m_employees(employees) {}

QString EmployeesReport::title() const {
    return QStringLiteral("Employees Report %1")
        .arg(QDate::currentDate().toString(Qt::ISODate));
}

QStringList EmployeesReport::header() const {
    return QStringList{QStringLiteral("Name"),   QStringLiteral("Position"),
                       QStringLiteral("Shift"),  QStringLiteral("Salary"),
                       QStringLiteral("Monthly Pay"), QStringLiteral("Rating")};
}

std::vector<QStringList> EmployeesReport::rows() const {
    /// @oop-concept Runtime Polymorphism / Object Pointers :: allTyped() hands back the concrete
    /// role objects (Waiter / Chef / Manager / Admin) behind `unique_ptr<Employee>`. Monthly Pay is
    /// then simply `e->monthlyPay()`: tips, overtime and bonuses are each computed by the role that
    /// owns that rule, and this report contains no `if (position == ...)` chain at all.
    const std::vector<std::unique_ptr<models::Employee>> staff = m_employees.allTyped();

    std::vector<QStringList> out;
    out.reserve(staff.size());

    core::Money payrollTotal;

    for (const std::unique_ptr<models::Employee>& e : staff) {
        if (!e)
            continue;

        const core::Money pay = e->monthlyPay();   // virtual dispatch on the real type
        payrollTotal += pay;

        out.push_back(QStringList{e->name(), e->roleName(), e->shift(), e->salary().toString(),
                                  pay.toString(), QString::number(e->performanceRating())});
    }

    out.push_back(QStringList{QStringLiteral("TOTAL"), QString(), QString(), QString(),
                              payrollTotal.toString(), QString()});
    return out;
}

} // namespace aluchop::services

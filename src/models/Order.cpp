/// \file
/// \brief Implementation of models::Order — the split/merge value semantics of
///        the central aggregate.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Order.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include <QLatin1Char>
#include <QLatin1String>
#include <QStringList>

#include "aluchop/core/Algorithms.hpp"
#include "aluchop/core/Exceptions.hpp"

namespace aluchop::models {
namespace {

/// \brief Whether two lines refer to the same dish and may therefore be combined.
///
/// Identity is the menu row, not the printed name: renaming "Chicken Momo" to
/// "Chicken Mo:Mo" must not split one line into two. When the dish has since been
/// deleted from the menu (`menuItemId == 0`) the frozen name snapshot is the only
/// identity left, so that is what is compared.
bool sameDish(const OrderItem& a, const OrderItem& b) noexcept
{
    if (a.menuItemId() != 0 && b.menuItemId() != 0)
        return a.menuItemId() == b.menuItemId();
    if (a.menuItemId() != b.menuItemId())
        return false;
    return a.name() == b.name();
}

/// \brief Fold \p incoming into the note already on \p line without losing either.
///
/// Two guests at one table can both order momo, one of them "no chilli". Merging
/// their quantities must not silently drop the instruction the kitchen needs.
void absorbNote(OrderItem& line, const QString& incoming)
{
    if (incoming.isEmpty())
        return;
    if (line.note().isEmpty()) {
        line.setNote(incoming);
        return;
    }
    if (line.note() == incoming || line.note().contains(incoming))
        return;
    line.setNote(line.note() + QLatin1String("; ") + incoming);
}

/// \brief The kitchen pipeline expressed as one table.
///
/// Keeping this in the model — rather than in the GUI that happens to draw the
/// buttons — is what makes it impossible for *any* code path to mark an order
/// Paid straight from Open.
bool isLegalTransition(OrderStatus from, OrderStatus to) noexcept
{
    if (from == to)
        return true; // idempotent: re-asserting the current status changes nothing

    switch (from) {
    case OrderStatus::Open:
        return to == OrderStatus::Pending || to == OrderStatus::Cancelled;
    case OrderStatus::Pending:
        return to == OrderStatus::Preparing || to == OrderStatus::Cancelled;
    case OrderStatus::Preparing:
        return to == OrderStatus::Ready;
    case OrderStatus::Ready:
        return to == OrderStatus::Served;
    case OrderStatus::Served:
        return to == OrderStatus::Paid;
    case OrderStatus::Paid:
    case OrderStatus::Cancelled:
        return false; // terminal
    }
    return false;
}

/// \brief Human label for a ticket, as opposed to the database token.
QString typeLabel(OrderType t)
{
    switch (t) {
    case OrderType::DineIn:
        return QStringLiteral("Dine-in");
    case OrderType::Takeaway:
        return QStringLiteral("Takeaway");
    case OrderType::Delivery:
        return QStringLiteral("Delivery");
    }
    return QStringLiteral("Dine-in");
}

/// \brief Human label for a ticket, as opposed to the database token.
QString statusLabel(OrderStatus s)
{
    switch (s) {
    case OrderStatus::Open:
        return QStringLiteral("Open");
    case OrderStatus::Pending:
        return QStringLiteral("Pending");
    case OrderStatus::Preparing:
        return QStringLiteral("Preparing");
    case OrderStatus::Ready:
        return QStringLiteral("Ready");
    case OrderStatus::Served:
        return QStringLiteral("Served");
    case OrderStatus::Paid:
        return QStringLiteral("Paid");
    case OrderStatus::Cancelled:
        return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Open");
}

constexpr int kTicketWidth = 40;

} // namespace

// ---------------------------------------------------------------------------
// Static data member.
//
// @oop-concept Static Members :: one counter shared by every Order object in the
// process; the dashboard reads it without touching the database.
// ---------------------------------------------------------------------------
int Order::s_openCount = 0;

// ---------------------------------------------------------------------------
// Construction / destruction.
//
// Counter contract (binding, ARCHITECTURE section 3.2): EVERY constructor
// increments and the destructor decrements exactly once. Incrementing only in
// the default constructor while always decrementing would drive the count
// negative the first time an order was copied for a split.
// ---------------------------------------------------------------------------

Order::Order()
    : m_createdAt(QDateTime::currentDateTimeUtc())
{
    ++s_openCount;
}

Order::Order(int id, QString orderNumber, OrderType type,
             int tableId, int customerId, int waiterId, QDateTime createdAt)
    : m_id(id)
    , m_orderNumber(std::move(orderNumber))
    , m_type(type)
    , m_tableId(tableId)
    , m_customerId(customerId)
    , m_waiterId(waiterId)
    , m_createdAt(std::move(createdAt))
{
    if (!m_createdAt.isValid())
        m_createdAt = QDateTime::currentDateTimeUtc();
    ++s_openCount;
}

Order::Order(const Order& other)
    : m_id(0)                       // a copy is a NEW, unsaved order …
    , m_orderNumber()               // … so it may not inherit a unique order number
    , m_type(other.m_type)
    , m_status(other.m_status)
    , m_tableId(other.m_tableId)
    , m_customerId(other.m_customerId)
    , m_waiterId(other.m_waiterId)
    , m_createdAt(other.m_createdAt)
    , m_note(other.m_note)
    , m_items(other.m_items)        // deep copy: the vector holds OrderItem VALUES
{
    ++s_openCount;
}

Order& Order::operator=(const Order& other)
{
    if (this == &other)
        return *this; // self-assignment must not wipe our own identity

    // Same semantics as the copy constructor: assigning an order over another
    // one produces a new unsaved order, because a split has to be persisted as a
    // fresh row and two rows may not share a primary key.
    m_id = 0;
    m_orderNumber.clear();
    m_type = other.m_type;
    m_status = other.m_status;
    m_tableId = other.m_tableId;
    m_customerId = other.m_customerId;
    m_waiterId = other.m_waiterId;
    m_createdAt = other.m_createdAt;
    m_note = other.m_note;
    m_items = other.m_items; // element-wise copy of the line objects
    return *this;
}

Order::Order(Order&& other) noexcept
    : m_id(other.m_id)                              // a MOVE is not a split:
    , m_orderNumber(std::move(other.m_orderNumber)) // identity is preserved, because
    , m_type(other.m_type)                          // this is what std::vector does when
    , m_status(other.m_status)                      // a repository hydrates rows.
    , m_tableId(other.m_tableId)
    , m_customerId(other.m_customerId)
    , m_waiterId(other.m_waiterId)
    , m_createdAt(std::move(other.m_createdAt))
    , m_note(std::move(other.m_note))
    , m_items(std::move(other.m_items))
{
    other.m_id = 0;
    other.m_status = OrderStatus::Open;
    other.m_tableId = 0;
    other.m_customerId = 0;
    other.m_waiterId = 0;
    other.m_items.clear();
    ++s_openCount;
}

Order& Order::operator=(Order&& other) noexcept
{
    if (this == &other)
        return *this;

    m_id = other.m_id;
    m_orderNumber = std::move(other.m_orderNumber);
    m_type = other.m_type;
    m_status = other.m_status;
    m_tableId = other.m_tableId;
    m_customerId = other.m_customerId;
    m_waiterId = other.m_waiterId;
    m_createdAt = std::move(other.m_createdAt);
    m_note = std::move(other.m_note);
    m_items = std::move(other.m_items);

    other.m_id = 0;
    other.m_status = OrderStatus::Open;
    other.m_tableId = 0;
    other.m_customerId = 0;
    other.m_waiterId = 0;
    other.m_items.clear();
    return *this;
}

Order::~Order()
{
    --s_openCount;
}

int Order::openOrderCount() noexcept
{
    return s_openCount;
}

// ---------------------------------------------------------------------------
// Items.
// ---------------------------------------------------------------------------

void Order::addItem(const OrderItem& item)
{
    for (OrderItem& line : m_items) {
        if (sameDish(line, item)) {
            // Quantities combine rather than producing a second identical line —
            // a ticket reading "2 x Momo" is what the kitchen can actually cook
            // from; "1 x Momo" twice is noise.
            line.setQty(line.qty() + item.qty());
            absorbNote(line, item.note());
            return;
        }
    }
    m_items.push_back(item);
}

void Order::addItem(int menuItemId, const QString& name, core::Money unitPrice, int qty)
{
    // Builds the line in place and then reuses the overload above, so the
    // merge-on-duplicate rule lives in exactly one place. OrderItem's constructor
    // performs the qty >= 1 validation and throws on failure.
    addItem(OrderItem(menuItemId, name, unitPrice, qty));
}

void Order::removeItemAt(std::size_t index)
{
    if (index >= m_items.size())
        throw std::out_of_range("Order::removeItemAt: index " + std::to_string(index)
                                + " is past the end (" + std::to_string(m_items.size())
                                + " lines)");
    m_items.erase(m_items.begin() + static_cast<std::vector<OrderItem>::difference_type>(index));
}

OrderItem& Order::operator[](std::size_t i)
{
    // Checked, unlike the raw std::vector subscript, because the index arrives
    // from a table selection in the GUI and a stale selection is a real event.
    if (i >= m_items.size())
        throw std::out_of_range("Order::operator[]: index " + std::to_string(i)
                                + " is past the end (" + std::to_string(m_items.size())
                                + " lines)");
    return m_items[i];
}

const OrderItem& Order::operator[](std::size_t i) const
{
    if (i >= m_items.size())
        throw std::out_of_range("Order::operator[] const: index " + std::to_string(i)
                                + " is past the end (" + std::to_string(m_items.size())
                                + " lines)");
    return m_items[i];
}

Order& Order::operator+=(const Order& other)
{
    // Merging an order into itself would double every line, which is never what
    // "put it all on one bill" means.
    if (this == &other)
        return *this;

    for (const OrderItem& line : other.m_items)
        addItem(line); // reuses the merge-on-duplicate rule

    // Only the lines move. This order keeps its own id, number, table and
    // status; the source is cancelled by OrderService afterwards.
    return *this;
}

core::Money Order::subtotal() const
{
    /// @oop-concept Function Template :: the same summation serves billing,
    /// dashboards and reports — nothing here adds tax, the line prices already
    /// include it.
    return core::sumMoney(m_items, [](const OrderItem& line) { return line.lineTotal(); });
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------

void Order::setStatus(OrderStatus s)
{
    if (!isLegalTransition(m_status, s))
        throw core::ValidationException(
            "Illegal order status transition " + toString(m_status).toStdString() + " -> "
            + toString(s).toStdString());
    m_status = s;
}

// ---------------------------------------------------------------------------
// Printing.
// ---------------------------------------------------------------------------

QString Order::toPrintableText() const
{
    const QString rule = QString(kTicketWidth, QLatin1Char('-'));
    QStringList out;

    out << QString(kTicketWidth, QLatin1Char('='));
    out << QStringLiteral("KITCHEN TICKET");
    out << rule;
    out << QStringLiteral("Order  : %1").arg(m_orderNumber.isEmpty() ? QStringLiteral("(unsaved)")
                                                                     : m_orderNumber);
    out << QStringLiteral("Type   : %1").arg(typeLabel(m_type));
    if (m_type == OrderType::DineIn)
        out << QStringLiteral("Table  : %1").arg(m_tableId > 0 ? QString::number(m_tableId)
                                                               : QStringLiteral("-"));
    out << QStringLiteral("Placed : %1")
               .arg(m_createdAt.isValid()
                        ? m_createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                        : QStringLiteral("-"));
    out << QStringLiteral("Status : %1").arg(statusLabel(m_status));
    out << rule;

    if (m_items.empty()) {
        out << QStringLiteral("(no items)");
    } else {
        for (const OrderItem& line : m_items) {
            out << QStringLiteral("%1 x %2").arg(line.qty()).arg(line.name());
            if (!line.note().isEmpty())
                out << QStringLiteral("      -> %1").arg(line.note());
        }
    }

    out << rule;
    if (!m_note.isEmpty())
        out << QStringLiteral("Note   : %1").arg(m_note);
    out << QStringLiteral("Items  : %1 line(s)").arg(m_items.size());
    out << QString(kTicketWidth, QLatin1Char('='));

    return out.join(QLatin1Char('\n'));
}

} // namespace aluchop::models

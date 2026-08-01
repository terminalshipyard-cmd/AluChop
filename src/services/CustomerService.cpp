/**
 * @file CustomerService.cpp
 * @brief The customer database: identity, loyalty, visit history and favourite dishes.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The loyalty rules live here rather than in `models::Customer` because they are *policy* — one
 * point per NPR 100 spent is a decision the restaurant could change tomorrow — whereas the model
 * owns only the invariants that must hold for any policy at all (points never go negative, a visit
 * count only ever moves forward).
 *
 * Visit history and favourites are derived, never stored: they are computed from the `orders`
 * table on demand, which is why a customer row stays small enough to load a few hundred at a time
 * into a table view.
 */

#include "aluchop/services/CustomerService.hpp"

#include <algorithm>
#include <map>
#include <utility>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/OrderItem.hpp"
#include "aluchop/services/AuditService.hpp"
#include "aluchop/services/NotificationService.hpp"

namespace aluchop::services {

namespace {

/// @brief The `NotificationService` domain name every customer mutation announces.
const QString kDomain = QStringLiteral("customers");

/// @brief How many rupees of spend earn one loyalty point (SPEC: `spent.wholeRupees() / 100`).
constexpr int kRupeesPerLoyaltyPoint = 100;

/// @brief How deep the favourites tally reaches into a customer's order history.
constexpr int kFavouritesHistoryDepth = 200;

/// @brief `"customer:<id>"` — the audit entity string for a guest.
QString customerEntity(int id) {
    return QStringLiteral("customer:%1").arg(id);
}

} // namespace

CustomerService::CustomerService(persistence::CustomerRepository& customers,
                                 persistence::OrderRepository& orders,
                                 AuditService& audit, NotificationService& notify)
    : m_customers(customers), m_orders(orders), m_audit(audit), m_notify(notify) {
    // Two repositories, because "customer" is genuinely a join of who they are (customers) and
    // what they have eaten (orders) — and the second half is far too large to live in the model.
}

// ---------------------------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------------------------

std::vector<models::Customer> CustomerService::all() const {
    try {
        return m_customers.findAll();   // CustomerRepository orders by name
    } catch (const core::AluChopException&) {
        return {};
    }
}

std::vector<models::Customer> CustomerService::search(const QString& term) const {
    const QString needle = term.trimmed();
    try {
        if (needle.isEmpty()) return m_customers.findAll();
        return m_customers.search(needle);
    } catch (const core::AluChopException&) {
        return {};
    }
}

std::optional<models::Customer> CustomerService::byId(int id) const {
    try {
        return m_customers.findById(id);
    } catch (const core::AluChopException&) {
        return std::nullopt;
    }
}

std::optional<models::Customer> CustomerService::byPhone(const QString& phone) const {
    const QString wanted = phone.trimmed();
    if (wanted.isEmpty()) return std::nullopt;
    try {
        return m_customers.byPhone(wanted);
    } catch (const core::AluChopException&) {
        return std::nullopt;
    }
}

std::vector<models::Order> CustomerService::visitHistory(int customerId, int limit) const {
    if (customerId <= 0 || limit <= 0) return {};
    try {
        return m_orders.forCustomer(customerId, limit);   // newest first
    } catch (const core::AluChopException&) {
        return {};
    }
}

std::vector<QString> CustomerService::favouriteItems(int customerId, int topN) const {
    if (customerId <= 0 || topN <= 0) return {};

    std::vector<models::Order> history;
    try {
        history = m_orders.forCustomer(customerId, kFavouritesHistoryDepth);
    } catch (const core::AluChopException&) {
        return {};
    }

    /// @oop-concept STL (map) :: dish name -> total quantity, accumulated across the whole history
    std::map<QString, int> tally;
    for (const models::Order& order : history) {
        // A cancelled order was never eaten, so it says nothing about what this guest likes.
        if (order.status() == models::OrderStatus::Cancelled) continue;
        for (const models::OrderItem& line : order.items())
            tally[line.name()] += line.qty();
    }

    std::vector<std::pair<QString, int>> ranked(tally.begin(), tally.end());

    /// @oop-concept STL Algorithms :: partial ordering is enough — only the top N are wanted
    const auto cut = static_cast<std::size_t>(topN) < ranked.size()
                         ? ranked.begin() + topN
                         : ranked.end();
    std::partial_sort(ranked.begin(), cut, ranked.end(),
                      [](const std::pair<QString, int>& a, const std::pair<QString, int>& b) {
                          if (a.second != b.second) return a.second > b.second;   // most eaten first
                          return a.first.localeAwareCompare(b.first) < 0;         // then A→Z
                      });

    std::vector<QString> out;
    out.reserve(static_cast<std::size_t>(topN));
    for (auto it = ranked.begin(); it != cut; ++it)
        out.push_back(it->first);
    return out;
}

// ---------------------------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------------------------

core::Result<int> CustomerService::create(const QString& name, const QString& phone,
                                          const QString& email) {
    const QString cleanPhone = phone.trimmed();
    try {
        // The Customer constructor validates name, phone shape and e-mail shape and throws
        // core::ValidationException naming the offending field — the GUI highlights exactly one box.
        /// @oop-concept Parameterised Constructor :: an invalid customer never exists, not even briefly
        models::Customer fresh(0, name.trimmed(), cleanPhone, email.trimmed());

        // The phone number is the natural key used at the till, and the column is UNIQUE. Checking
        // it here turns a driver constraint error into a sentence the cashier can act on.
        if (!cleanPhone.isEmpty() && m_customers.byPhone(cleanPhone))
            throw core::ValidationException("that phone number is already registered", "phone");

        const int newId = m_customers.insert(fresh);

        m_audit.log(QStringLiteral("CUST_CREATE"), customerEntity(newId), core::Money(),
                    fresh.name());
        m_notify.notify(QStringLiteral("Customer added"),
                        QStringLiteral("%1 is now in the loyalty database.").arg(fresh.name()),
                        static_cast<int>(models::NoticeLevel::Success));
        m_notify.announceDataChanged(kDomain);
        return core::Result<int>::ok(newId);
    } catch (const core::AluChopException& e) {
        return core::Result<int>::err(QString::fromStdString(e.message()));
    }
}

core::Result<void> CustomerService::update(const models::Customer& c) {
    try {
        if (c.id() <= 0)
            throw core::ValidationException("that customer has not been saved yet", "id");
        if (!m_customers.findById(c.id()))
            throw core::ValidationException("that customer no longer exists", "id");

        // Re-parenting a phone number onto somebody else's record would break the till lookup.
        if (!c.phone().isEmpty()) {
            const std::optional<models::Customer> owner = m_customers.byPhone(c.phone());
            if (owner && owner->id() != c.id())
                throw core::ValidationException("that phone number belongs to another customer",
                                                "phone");
        }

        m_customers.update(c);

        m_audit.log(QStringLiteral("CUST_UPDATE"), customerEntity(c.id()), core::Money(), c.name());
        m_notify.announceDataChanged(kDomain);
        return core::Result<void>::ok();
    } catch (const core::AluChopException& e) {
        return core::Result<void>::err(QString::fromStdString(e.message()));
    }
}

core::Result<void> CustomerService::remove(int customerId) {
    try {
        const std::optional<models::Customer> existing = m_customers.findById(customerId);
        if (!existing)
            throw core::ValidationException("that customer no longer exists", "id");

        // `orders.customer_id` is ON DELETE SET NULL: past orders keep their own name and price
        // snapshots and simply stop pointing at anybody, so revenue history survives the deletion.
        m_customers.removeById(customerId);

        m_audit.log(QStringLiteral("CUST_DELETE"), customerEntity(customerId), core::Money(),
                    existing->name());
        m_notify.notify(QStringLiteral("Customer removed"),
                        QStringLiteral("%1 has been deleted; their past orders are kept.")
                            .arg(existing->name()),
                        static_cast<int>(models::NoticeLevel::Warning));
        m_notify.announceDataChanged(kDomain);
        return core::Result<void>::ok();
    } catch (const core::AluChopException& e) {
        return core::Result<void>::err(QString::fromStdString(e.message()));
    }
}

core::Result<void> CustomerService::recordVisit(int customerId, core::Money spent) {
    try {
        std::optional<models::Customer> guest = m_customers.findById(customerId);
        if (!guest)
            throw core::ValidationException("that customer no longer exists", "customerId");
        if (spent.isNegative())
            throw core::ValidationException("a visit cannot have negative spend", "spent");

        /// @oop-concept Increment Operator :: `++customer` is literally "one more visit" — the
        /// domain meaning of ++, which is exactly why Customer overloads it instead of exposing a
        /// setVisits() that any caller could set to anything
        ++(*guest);

        // Loyalty policy: one point per NPR 100 of settled spend, rounded down. Money is integer
        // paisa, so this is exact integer arithmetic with no floating-point drift anywhere.
        const int earned = static_cast<int>(spent.wholeRupees() / kRupeesPerLoyaltyPoint);
        if (earned > 0) guest->addLoyaltyPoints(earned);

        m_customers.update(*guest);

        m_audit.log(QStringLiteral("CUST_VISIT"), customerEntity(customerId), spent,
                    QStringLiteral("visit %1, +%2 pts").arg(guest->visits()).arg(earned));
        if (earned > 0) {
            m_notify.notify(QStringLiteral("Loyalty points awarded"),
                            QStringLiteral("%1 earned %2 point(s) — balance %3.")
                                .arg(guest->name())
                                .arg(earned)
                                .arg(guest->loyaltyPoints()),
                            static_cast<int>(models::NoticeLevel::Success));
        }
        m_notify.announceDataChanged(kDomain);
        return core::Result<void>::ok();
    } catch (const core::AluChopException& e) {
        return core::Result<void>::err(QString::fromStdString(e.message()));
    }
}

core::Result<void> CustomerService::redeemPoints(int customerId, int points) {
    try {
        std::optional<models::Customer> guest = m_customers.findById(customerId);
        if (!guest)
            throw core::ValidationException("that customer no longer exists", "customerId");

        // Customer::redeemPoints throws when the balance is too small; the check mirrors the
        // `CHECK (loyalty_points >= 0)` column constraint so the message reaches the user rather
        // than the driver.
        guest->redeemPoints(points);
        m_customers.update(*guest);

        m_audit.log(QStringLiteral("CUST_REDEEM"), customerEntity(customerId), core::Money(),
                    QStringLiteral("-%1 pts, balance %2").arg(points).arg(guest->loyaltyPoints()));
        m_notify.notify(QStringLiteral("Points redeemed"),
                        QStringLiteral("%1 spent %2 point(s); %3 remain.")
                            .arg(guest->name())
                            .arg(points)
                            .arg(guest->loyaltyPoints()),
                        static_cast<int>(models::NoticeLevel::Info));
        m_notify.announceDataChanged(kDomain);
        return core::Result<void>::ok();
    } catch (const core::AluChopException& e) {
        return core::Result<void>::err(QString::fromStdString(e.message()));
    }
}

} // namespace aluchop::services

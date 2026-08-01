/**
 * @file BillingService.cpp
 * @brief Implementation of bill computation, discounting and settlement.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * @warning **Menu prices are already tax-inclusive.** There is not one line in this file that adds
 * tax to a total. The receipt printed by `models::Bill::toPrintableText()` may *display* the tax
 * already contained in the price as a reverse-computed breakdown, but the amount the guest hands
 * over is, and only ever is:
 *
 *     total = subtotal - discount + serviceCharge
 *
 * The two rules that make that line honest:
 *  - **Discounts never stack.** The candidates are the promo-code discount and the staff discount;
 *    the single LARGEST wins and the others are discarded, never summed.
 *  - **The service charge is applied after the discount** and is a percentage of the *subtotal*,
 *    so discounting a bill can never quietly shrink the service the staff earned.
 */

#include "aluchop/services/BillingService.hpp"

#include <utility>

#include <QDate>
#include <QDateTime>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Logger.hpp"
#include "aluchop/models/Customer.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/models/Promo.hpp"
#include "aluchop/models/StaffCustomer.hpp"
#include "aluchop/persistence/Database.hpp"
#include "aluchop/services/AuditService.hpp"
#include "aluchop/services/CustomerService.hpp"
#include "aluchop/services/EmployeeService.hpp"
#include "aluchop/services/NotificationService.hpp"

namespace aluchop::services {

namespace {

QString orderTag(int orderId) {
    return QStringLiteral("order:%1").arg(orderId);
}

QString exceptionText(const core::AluChopException& ex) {
    return QString::fromUtf8(ex.what());
}

/// @brief One competing discount: how much comes off, and what the receipt calls it.
///
/// @oop-concept Structures :: pure data with no invariant to protect — three independent fields
/// whose only job is to be compared against the other candidates.
struct DiscountCandidate {
    core::Money amount;   ///< Absolute amount off the tax-inclusive subtotal.
    QString label;        ///< Printed beside the discount line, e.g. "Staff 10%".
    QString promoCode;    ///< Non-empty only when this candidate came from a promo code.
};

/// @brief Loyalty rule: one point per NPR 100 actually paid.
int pointsFor(core::Money total) noexcept {
    const long long rupees = total.wholeRupees();
    return rupees > 0 ? static_cast<int>(rupees / 100) : 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

/// @oop-concept Pass by Reference :: repositories and sibling services are borrowed, never owned.
BillingService::BillingService(persistence::OrderRepository& orders,
                               persistence::PaymentRepository& payments,
                               persistence::PromoRepository& promos, CustomerService& customers,
                               EmployeeService& employees, AuditService& audit,
                               NotificationService& notify)
    : m_orders(orders),
      m_payments(payments),
      m_promos(promos),
      m_customers(customers),
      m_employees(employees),
      m_audit(audit),
      m_notify(notify) {}

// ---------------------------------------------------------------------------
// Preparing the bill
// ---------------------------------------------------------------------------

core::Result<models::Bill> BillingService::prepareBill(int orderId, const QString& promoCode,
                                                       int serviceChargePercent) {
    using R = core::Result<models::Bill>;

    if (serviceChargePercent < 0 || serviceChargePercent > 100)
        return R::err(QStringLiteral("Service charge must be between 0 and 100 percent."));

    try {
        const auto order = m_orders.loadOrder(orderId);
        if (!order)
            return R::err(QStringLiteral("Order %1 no longer exists.").arg(orderId));
        if (order->status() == models::OrderStatus::Paid)
            return R::err(QStringLiteral("%1 has already been settled.").arg(order->orderNumber()));
        if (order->status() != models::OrderStatus::Served)
            return R::err(QStringLiteral("%1 has not been served yet — a bill is only raised "
                                         "once the food has gone out.").arg(order->orderNumber()));
        if (order->itemCount() == 0)
            return R::err(QStringLiteral("There is nothing on %1 to bill.")
                              .arg(order->orderNumber()));

        // The Bill constructor snapshots the lines and the tax-inclusive subtotal. From here on
        // the order could be edited or the menu re-priced without changing what the guest owes.
        models::Bill bill(*order);
        const core::Money subtotal = bill.subtotal();

        // --- the competing discounts ---------------------------------------------------------
        // Deliberately gathered as candidates and then compared, rather than applied one after
        // another: that is the shape of "never stack" written directly into the control flow.
        DiscountCandidate best{core::Money::zero(), QString(), QString()};

        const QString code = promoCode.trimmed().toUpper();
        if (!code.isEmpty()) {
            const auto promo = m_promos.byCode(code);
            if (!promo) {
                m_notify.notify(QStringLiteral("Promo ignored"),
                                QStringLiteral("No such code: %1").arg(code), 2);
            } else if (!promo->isValidOn(QDate::currentDate(), subtotal)) {
                m_notify.notify(QStringLiteral("Promo ignored"),
                                QStringLiteral("%1 is expired, switched off, or the bill is below "
                                               "its minimum order.").arg(code), 2);
            } else {
                best = DiscountCandidate{promo->discountFor(subtotal),
                                         QStringLiteral("Promo %1").arg(code), code};
            }
        }

        if (order->customerId() != 0) {
            /// @oop-concept Virtual Base Class :: staffCustomerFor() returns the fused
            /// StaffCustomer — Employee and Customer over ONE Person subobject — which is the only
            /// reason a single object can answer both "are you on the payroll?" and "what is your
            /// loyalty balance?" without carrying two identities.
            const auto staff = m_employees.staffCustomerFor(order->customerId());
            if (staff) {
                const int pct = staff->staffDiscountPercent();
                const core::Money staffOff = subtotal.percent(pct);
                // Strictly greater: a tie leaves the promo in place, because the guest asked for it.
                if (best.amount < staffOff)
                    best = DiscountCandidate{staffOff,
                                             QStringLiteral("Staff %1%").arg(pct), QString()};
            }
        }

        if (!best.amount.isZero()) {
            bill.setDiscount(best.amount, best.label);
            if (!best.promoCode.isEmpty())
                bill.setPromoCode(best.promoCode);
        }

        // --- service charge, AFTER the discount ----------------------------------------------
        if (serviceChargePercent > 0)
            bill.setServiceCharge(subtotal.percent(serviceChargePercent));

        // total() == subtotal - discount + serviceCharge. No tax term exists, here or in Bill.
        return R::ok(std::move(bill));
    } catch (const core::AluChopException& ex) {
        return R::err(exceptionText(ex));
    }
}

// ---------------------------------------------------------------------------
// Taking the money
// ---------------------------------------------------------------------------

core::Result<models::Payment> BillingService::settle(int orderId, models::Bill& bill,
                                                     models::PaymentMethod method,
                                                     core::Money tendered, int cashierUserId) {
    using R = core::Result<models::Payment>;

    if (bill.isSettled())
        return R::err(QStringLiteral("This bill has already been settled."));
    if (bill.orderId() != orderId)
        return R::err(QStringLiteral("That bill belongs to a different order."));

    const core::Money total = bill.total();

    // Cash is the only tender that can produce change; card and wallet are exact by construction.
    core::Money paid = total;
    core::Money change = core::Money::zero();
    if (method == models::PaymentMethod::Cash) {
        try {
            change = changeFor(total, tendered);   // throws when the tender is short
        } catch (const core::ValidationException&) {
            return R::err(QStringLiteral("Insufficient tender: %1 does not cover %2.")
                              .arg(tendered.toString(), total.toString()));
        }
        paid = tendered;
    }

    try {
        const auto order = m_orders.loadOrder(orderId);
        if (!order)
            return R::err(QStringLiteral("Order %1 no longer exists.").arg(orderId));
        if (order->status() == models::OrderStatus::Paid)
            return R::err(QStringLiteral("%1 has already been settled.").arg(order->orderNumber()));
        if (order->status() != models::OrderStatus::Served)
            return R::err(QStringLiteral("%1 has not been served yet.").arg(order->orderNumber()));

        int promoId = 0;
        if (!bill.promoCode().isEmpty()) {
            const auto promo = m_promos.byCode(bill.promoCode());
            if (promo) promoId = promo->id();
        }

        models::Payment payment;
        payment.setOrderId(orderId);
        payment.setMethod(method);
        payment.setSubtotal(bill.subtotal());
        payment.setDiscount(bill.discount());
        payment.setServiceCharge(bill.serviceCharge());
        payment.setTotal(total);
        payment.setTendered(paid);
        payment.setChange(change);
        payment.setPromoId(promoId);
        payment.setReceivedByUserId(cashierUserId);
        payment.setPaidAt(QDateTime::currentDateTimeUtc());

        const int customerId = order->customerId();

        // One unit of work: the money row, the order's closing status and the loyalty award land
        // together or not at all. Database::transaction rolls back and rethrows on any throw.
        persistence::Database::instance().transaction([&] {
            payment.setId(m_payments.insert(payment));
            m_orders.updateStatus(orderId, models::OrderStatus::Paid);

            if (customerId != 0) {
                // Points are earned on money actually taken. The visit itself was already counted
                // when the order reached Served, so nothing is double-credited.
                const int earned = pointsFor(total);
                if (earned > 0) {
                    auto customer = m_customers.byId(customerId);
                    if (customer) {
                        customer->addLoyaltyPoints(earned);
                        const auto saved = m_customers.update(*customer);
                        if (saved.isErr())
                            core::Logger::instance().warn(
                                QStringLiteral("loyalty not credited on order %1: %2")
                                    .arg(orderId).arg(saved.error()));
                    }
                }
            }
        });

        /// @oop-concept Friend Class :: Bill::settle is private and BillingService is its only
        /// friend, so this single line is the ONLY place in the entire application where a bill can
        /// become "paid" — and it runs after the payment row is safely committed, never before.
        bill.settle(method, paid, change);

        m_audit.log(QStringLiteral("ORDER_PAID"), orderTag(orderId), total,
                    QStringLiteral("%1 %2").arg(models::toString(method), total.toString()));
        m_notify.announceDataChanged(QStringLiteral("orders"));
        m_notify.announceDataChanged(QStringLiteral("payments"));
        m_notify.announceDataChanged(QStringLiteral("customers"));
        m_notify.notify(QStringLiteral("Payment taken"),
                        QStringLiteral("%1 settled — %2")
                            .arg(order->orderNumber(), total.toString()),
                        1);

        return R::ok(std::move(payment));
    } catch (const core::AluChopException& ex) {
        core::Logger::instance().error(
            QStringLiteral("settlement of order %1 failed: %2").arg(orderId).arg(exceptionText(ex)));
        return R::err(exceptionText(ex));
    }
}

/// @oop-concept Constant Member Functions :: a pure calculation cannot touch the service, so it is
/// const — and because a short tender is a broken rule rather than an expected outcome, it throws
/// instead of returning a sentinel amount.
core::Money BillingService::changeFor(core::Money total, core::Money tendered) const {
    if (tendered < total)
        throw core::ValidationException("insufficient tender", "tendered");
    return tendered - total;
}

QString BillingService::receiptText(const models::Bill& bill) const {
    // The receipt is the Bill's own job (IPrintable); billing does not re-render money anywhere.
    return bill.toPrintableText();
}

} // namespace aluchop::services

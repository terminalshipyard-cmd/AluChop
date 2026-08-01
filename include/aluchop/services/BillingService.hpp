#pragma once

/**
 * @file BillingService.hpp
 * @brief Bill computation, discounting and settlement.
 *
 * @warning **Menu prices are already tax-inclusive.** Nothing in this service adds tax to a total.
 * A receipt may *display* the tax already contained in the price as a breakdown line, but the
 * amount the guest pays is `subtotal - discount + serviceCharge` and nothing else.
 *
 * Discounts never stack. The candidates are the promo-code discount, the staff discount (when the
 * order's customer is also an active employee) and a manual discount; the single largest wins.
 * The service charge is a percentage of the subtotal and is applied *after* the discount.
 */

#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/core/Result.hpp"
#include "aluchop/models/Bill.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/Payment.hpp"
#include "aluchop/persistence/OrderRepository.hpp"
#include "aluchop/persistence/PaymentRepository.hpp"
#include "aluchop/persistence/PromoRepository.hpp"

namespace aluchop::services {

class CustomerService;
class EmployeeService;
class AuditService;
class NotificationService;

/**
 * @brief Turns a served order into a bill, then into a settled payment.
 *
 * `models::Bill` names this class as a friend, so only the billing engine can mark a bill settled;
 * every other holder of a Bill sees an immutable snapshot.
 */
class BillingService {
public:
    BillingService(persistence::OrderRepository& orders, persistence::PaymentRepository& payments,
                   persistence::PromoRepository& promos, CustomerService& customers,
                   EmployeeService& employees, AuditService& audit, NotificationService& notify);

    /**
     * @brief Builds the bill for a served order.
     * @param promoCode optional code; ignored when invalid, expired or under its minimum order.
     * @param serviceChargePercent percentage of the subtotal added after the discount (0 = none).
     * @return the computed bill, or an error when the order is missing or not billable.
     *
     * /// @oop-concept Default Arguments :: most bills have no promo and no service charge
     */
    core::Result<models::Bill> prepareBill(int orderId,
                                           const QString& promoCode = QString(),
                                           int serviceChargePercent = 0);

    /**
     * @brief Takes payment and closes the order.
     * @details Cash requires `tendered >= total` and returns the difference as change;
     *          card and wallet force `tendered == total` and zero change. One database
     *          transaction inserts the payment, moves the order to Paid and awards loyalty points.
     * @param[in,out] bill marked settled on success (this class is `Bill`'s friend).
     * @return the persisted payment, or an error (insufficient tender, DB failure).
     *
     * /// @oop-concept Friend Class :: only BillingService may call Bill::settle — settlement is
     * /// the one state change a bill is allowed to undergo, and only from here
     */
    core::Result<models::Payment> settle(int orderId, models::Bill& bill,
                                         models::PaymentMethod method, core::Money tendered,
                                         int cashierUserId);

    /**
     * @brief Change owed on a cash payment.
     * @throws core::ValidationException("insufficient tender") when `tendered < total`.
     * @oop-concept Constant Member Functions :: a pure calculation cannot touch the service
     */
    core::Money changeFor(core::Money total, core::Money tendered) const;

    /// @brief Printable receipt body, produced by `Bill::toPrintableText()`.
    QString receiptText(const models::Bill& bill) const;

private:
    persistence::OrderRepository& m_orders;
    persistence::PaymentRepository& m_payments;
    persistence::PromoRepository& m_promos;
    CustomerService& m_customers;
    EmployeeService& m_employees;
    AuditService& m_audit;
    NotificationService& m_notify;
};

} // namespace aluchop::services

#pragma once

/// \file
/// \brief Bill — the immutable money snapshot taken from an order at the till.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <iosfwd>
#include <vector>

#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/Interfaces.hpp"
#include "aluchop/models/OrderItem.hpp"

// BillingService is the sole friend of Bill; declared here so the friendship can
// be granted without dragging the whole services layer into the models layer
// (which would invert the dependency direction of the architecture).
namespace aluchop { namespace services { class BillingService; } }

namespace aluchop::models {

class Order; ///< Forward declaration — Bill is *built from* an Order but does not contain one.

/// @oop-concept Multiple Inheritance :: a bill is printable AND discountable
/// \brief What the guest owes: the lines, the discount, the service charge, the total.
///
/// A Bill is a snapshot, not a live view. Once prepared it holds its own copy of
/// the lines, so re-pricing the menu or editing the order afterwards cannot
/// change a bill that has already been shown to a guest.
///
/// \warning **Prices are tax-inclusive.** total() is
///          `subtotal − discount + serviceCharge`. There is no tax term in this
///          class, and none may ever be added.
class Bill : public IPrintable, public IDiscountable {
    /// @oop-concept Friend Class :: ONLY BillingService may settle a bill — the mutators are private
    ///
    /// settle() writes the tender, the change and the settled flag in one atomic
    /// step. Exposing it publicly would let any widget mark a bill paid without
    /// a Payment row ever reaching the database, so instead exactly one class is
    /// trusted with it. This is friendship used to *narrow* access, not widen it.
    friend class aluchop::services::BillingService;

public:
    /// \brief Default constructor — an empty, unsettled bill.
    Bill() = default;

    /// @oop-concept Parameterised Constructor :: a bill is derived from exactly one order
    /// \brief Snapshot an order: its lines, its subtotal and its identifiers.
    /// \param order Source order, normally in status Served.
    ///
    /// `explicit` because an Order is not a Bill and must never convert into one
    /// silently in a function call.
    explicit Bill(const Order& order);

    /// \brief Source order's primary key.
    int orderId() const noexcept { return m_orderId; }

    /// \brief Source order's human-facing number.
    const QString& orderNumber() const noexcept { return m_orderNumber; }

    /// @oop-concept Return by Reference :: the receipt renders the lines without copying them
    /// \brief The snapshotted lines.
    const std::vector<OrderItem>& items() const noexcept { return m_items; }

    /// \brief Sum of the lines, tax-inclusive.
    core::Money subtotal() const noexcept { return m_subtotal; }

    /// \brief The discount currently applied (IDiscountable).
    core::Money discount() const override { return m_discount; }

    /// \brief Apply a discount (IDiscountable).
    /// \param amount Absolute amount off; never a percentage.
    /// \param label  Reason printed on the receipt, e.g. "Staff 10%" or "PROMO NEWYEAR".
    /// \throws core::ValidationException when \p amount is negative or exceeds
    ///         subtotal() — a bill may reach zero but never go below it.
    void setDiscount(core::Money amount, const QString& label) override;

    /// \brief Reason shown next to the discount line.
    const QString& discountLabel() const noexcept { return m_discountLabel; }

    /// \brief Service charge, applied **after** the discount.
    core::Money serviceCharge() const noexcept { return m_serviceCharge; }

    /// \brief Set the service charge.
    /// \throws core::ValidationException when \p v is negative.
    void setServiceCharge(core::Money v);

    /// \brief Promo code that produced the discount; empty when none applied.
    const QString& promoCode() const noexcept { return m_promoCode; }
    /// \brief Record the promo code used.
    void setPromoCode(const QString& c) { m_promoCode = c; }

    /// @oop-concept Constant Member Functions :: the total is derived, never stored twice
    /// \brief `subtotal − discount + serviceCharge`.
    ///
    /// \note Prices are tax-INCLUSIVE. This total NEVER adds tax.
    core::Money total() const noexcept { return m_subtotal - m_discount + m_serviceCharge; }

    /// \brief Whether payment has been taken.
    bool isSettled() const noexcept { return m_settled; }

    /// \brief Tender type used; meaningful only once isSettled() is true.
    PaymentMethod method() const noexcept { return m_method; }

    /// \brief Amount handed over; equals total() for card and wallet.
    core::Money tendered() const noexcept { return m_tendered; }

    /// \brief Change returned; always zero for card and wallet.
    core::Money change() const noexcept { return m_change; }

    /// \brief The full receipt, including the line
    ///        "All prices are inclusive of tax." (IPrintable).
    QString toPrintableText() const override;

    /// @oop-concept Stream Insertion Operator :: bills stream themselves into text receipts / CSV
    /// \brief Write the bill to a `std::ostream`.
    ///
    /// A free function rather than a member, because the stream must be the
    /// left-hand operand; `friend` because it needs the private figures without
    /// forcing the class to expose a getter for each one.
    /// \param os Destination stream.
    /// \param b  Bill to write.
    /// \return \p os, so insertions chain.
    friend std::ostream& operator<<(std::ostream& os, const Bill& b);

private:
    /// \brief Record payment — callable only by BillingService.
    /// \param m        Tender type.
    /// \param tendered Amount handed over.
    /// \param change   Change returned.
    void settle(PaymentMethod m, core::Money tendered, core::Money change);

    int m_orderId = 0;                       ///< Source order's primary key.
    QString m_orderNumber;                   ///< Source order's number.
    std::vector<OrderItem> m_items;          ///< Snapshotted lines.
    core::Money m_subtotal;                  ///< Sum of the lines, tax-inclusive.
    core::Money m_discount;                  ///< Absolute discount applied.
    core::Money m_serviceCharge;             ///< Service charge, applied after the discount.
    core::Money m_tendered;                  ///< Amount handed over.
    core::Money m_change;                    ///< Change returned.
    QString m_discountLabel;                 ///< Reason for the discount.
    QString m_promoCode;                     ///< Promo code used, if any.
    PaymentMethod m_method = PaymentMethod::Cash; ///< Tender type.
    bool m_settled = false;                  ///< Set by settle() only.
};

} // namespace aluchop::models

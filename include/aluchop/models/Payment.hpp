#pragma once

/// \file
/// \brief Payment — the persisted record of a settled bill.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QDateTime>

#include "aluchop/core/Money.hpp"
#include "aluchop/models/Enums.hpp"

namespace aluchop::models {

/// \brief One row of the `payments` ledger.
///
/// Where Bill is the transient object the cashier works with, Payment is what
/// survives: the money figures are copied out of the bill at settlement and are
/// never recomputed, so every revenue report is reading exactly what the guest
/// paid, not a figure re-derived from a menu that may have changed since.
///
/// The class is intentionally a plain aggregate of validated setters — the
/// arithmetic all happened in BillingService before it got here.
///
/// \warning Every amount is tax-inclusive. `total = subtotal − discount +
///          serviceCharge`; no tax term exists.
class Payment {
public:
    /// \brief Default constructor — filled in field by field by BillingService.
    Payment() = default;

    /// \brief Database primary key.
    int id() const noexcept { return m_id; }
    /// \brief Assign the primary key after an INSERT.
    void setId(int v) { m_id = v; }

    /// \brief Order that was settled.
    int orderId() const noexcept { return m_orderId; }
    /// \brief Set the settled order.
    void setOrderId(int v) { m_orderId = v; }

    /// \brief Tender type used.
    PaymentMethod method() const noexcept { return m_method; }
    /// \brief Set the tender type.
    void setMethod(PaymentMethod m) { m_method = m; }

    /// \brief Line subtotal at settlement, tax-inclusive.
    core::Money subtotal() const noexcept { return m_subtotal; }
    /// \brief Set the line subtotal.
    void setSubtotal(core::Money v) { m_subtotal = v; }

    /// \brief Discount granted.
    core::Money discount() const noexcept { return m_discount; }
    /// \brief Set the discount granted.
    void setDiscount(core::Money v) { m_discount = v; }

    /// \brief Service charge added after the discount.
    core::Money serviceCharge() const noexcept { return m_serviceCharge; }
    /// \brief Set the service charge.
    void setServiceCharge(core::Money v) { m_serviceCharge = v; }

    /// \brief Amount actually owed.
    core::Money total() const noexcept { return m_total; }
    /// \brief Set the amount owed.
    void setTotal(core::Money v) { m_total = v; }

    /// \brief Amount handed over by the guest.
    core::Money tendered() const noexcept { return m_tendered; }
    /// \brief Set the amount handed over.
    void setTendered(core::Money v) { m_tendered = v; }

    /// \brief Change returned to the guest.
    core::Money change() const noexcept { return m_change; }
    /// \brief Set the change returned.
    void setChange(core::Money v) { m_change = v; }

    /// \brief Promo row that produced the discount, or 0.
    int promoId() const noexcept { return m_promoId; }
    /// \brief Set the promo row.
    void setPromoId(int v) { m_promoId = v; }

    /// \brief Login account of the cashier who took the money.
    int receivedByUserId() const noexcept { return m_receivedBy; }
    /// \brief Set the cashier's login account.
    void setReceivedByUserId(int v) { m_receivedBy = v; }

    /// \brief Settlement timestamp (UTC) — the axis every revenue report groups by.
    QDateTime paidAt() const noexcept { return m_paidAt; }
    /// \brief Set the settlement timestamp.
    void setPaidAt(QDateTime t) { m_paidAt = t; }

private:
    int m_id = 0;                                 ///< Primary key.
    int m_orderId = 0;                            ///< Settled order.
    int m_promoId = 0;                            ///< Promo used, or 0.
    int m_receivedBy = 0;                         ///< Cashier's login account.
    PaymentMethod m_method = PaymentMethod::Cash; ///< Tender type.
    core::Money m_subtotal;                       ///< Tax-inclusive line subtotal.
    core::Money m_discount;                       ///< Discount granted.
    core::Money m_serviceCharge;                  ///< Service charge.
    core::Money m_total;                          ///< Amount owed.
    core::Money m_tendered;                       ///< Amount handed over.
    core::Money m_change;                         ///< Change returned.
    QDateTime m_paidAt;                           ///< Settlement timestamp (UTC).
};

} // namespace aluchop::models

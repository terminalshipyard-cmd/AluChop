#pragma once

/// \file
/// \brief Promo — a discount code with a validity window.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QDate>
#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/models/Enums.hpp"

namespace aluchop::models {

/// \brief A redeemable promotion: percentage or flat, time-boxed, with a floor.
///
/// The two rules a promo has to answer — *may I use this?* and *how much comes
/// off?* — both live here rather than in BillingService, so a promo cannot be
/// interpreted one way at the till and another way in a report.
///
/// \warning Prices are tax-inclusive, so a discount is simply subtracted from
///          the tax-inclusive subtotal. There is no net/gross distinction to
///          unwind.
class Promo {
public:
    /// \brief Default constructor — used by PromoRepository before hydration.
    Promo() = default;

    /// \brief Database primary key.
    int id() const noexcept { return m_id; }
    /// \brief Assign the primary key after an INSERT.
    void setId(int v) { m_id = v; }

    /// \brief The code a guest quotes, always stored upper-case.
    const QString& code() const noexcept { return m_code; }
    /// \brief Set the code.
    /// \param v Raw code; stored upper-cased so lookup is case-insensitive.
    /// \throws core::ValidationException when \p v trims to empty.
    void setCode(const QString& v);

    /// \brief Whether this promo takes a percentage or a flat amount off.
    PromoKind kind() const noexcept { return m_kind; }
    /// \brief Set the promo kind.
    void setKind(PromoKind k) { m_kind = k; }

    /// \brief Percentage off; meaningful only when kind() is PromoKind::Percent.
    int percent() const noexcept { return m_percent; }
    /// \brief Set the percentage off.
    /// \throws core::ValidationException when \p p is outside 0..100.
    void setPercent(int p);

    /// \brief Flat amount off; meaningful only when kind() is PromoKind::Flat.
    core::Money flatAmount() const noexcept { return m_flat; }
    /// \brief Set the flat amount off.
    /// \throws core::ValidationException when \p v is negative.
    void setFlatAmount(core::Money v);

    /// \brief Minimum order subtotal before the promo may be used.
    core::Money minOrder() const noexcept { return m_minOrder; }
    /// \brief Set the minimum order subtotal.
    void setMinOrder(core::Money v) { m_minOrder = v; }

    /// \brief First day the promo is valid; invalid QDate means "no lower bound".
    QDate validFrom() const noexcept { return m_validFrom; }
    /// \brief Set the first valid day.
    void setValidFrom(QDate d) { m_validFrom = d; }

    /// \brief Last day the promo is valid; invalid QDate means "no upper bound".
    QDate validTo() const noexcept { return m_validTo; }
    /// \brief Set the last valid day.
    void setValidTo(QDate d) { m_validTo = d; }

    /// \brief Whether the promo has been switched on.
    bool isActive() const noexcept { return m_active; }
    /// \brief Switch the promo on or off without deleting it.
    void setActive(bool a) { m_active = a; }

    /// @oop-concept Constant Member Functions :: the redemption rules interrogate, never mutate
    /// \brief Whether the promo may be redeemed on \p day for this basket.
    /// \param day            Day of redemption.
    /// \param orderSubtotal  Tax-inclusive subtotal of the order.
    /// \return `true` only when the promo is active, \p day falls inside the
    ///         validity window (an invalid bound is treated as open-ended) and
    ///         \p orderSubtotal reaches minOrder().
    bool isValidOn(QDate day, core::Money orderSubtotal) const;

    /// \brief How much this promo takes off \p subtotal.
    /// \param subtotal Tax-inclusive subtotal of the order.
    /// \return For PromoKind::Percent, `subtotal.percent(percent())`; for
    ///         PromoKind::Flat, `min(flatAmount(), subtotal)` — a flat promo
    ///         larger than the bill discounts the bill to zero and no further,
    ///         so the till can never owe the guest money.
    core::Money discountFor(core::Money subtotal) const;

private:
    int m_id = 0;                          ///< Primary key.
    int m_percent = 0;                     ///< Percentage off, 0..100.
    QString m_code;                        ///< Upper-cased redemption code.
    PromoKind m_kind = PromoKind::Percent; ///< Percentage or flat.
    core::Money m_flat;                    ///< Flat amount off, in paisa.
    core::Money m_minOrder;                ///< Minimum qualifying subtotal.
    QDate m_validFrom;                     ///< First valid day; invalid = open-ended.
    QDate m_validTo;                       ///< Last valid day; invalid = open-ended.
    bool m_active = true;                  ///< On/off switch.
};

} // namespace aluchop::models

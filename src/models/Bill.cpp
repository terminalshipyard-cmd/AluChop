/// \file
/// \brief Implementation of models::Bill — the receipt, the discount rules and
///        the friend-only settlement.
///
/// \warning **Every price handled here is already tax-inclusive.** The receipt
///          prints a VAT line, but that line is a *reverse* computation carried
///          out of the inclusive total: for an inclusive amount `P` at 13% the
///          tax already contained in it is `P - P/1.13`. No code path in this
///          file — or in this application — ever adds tax on top of a price.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Bill.hpp"

#include <cstdint>
#include <ostream>
#include <string>

#include <QLatin1Char>
#include <QLatin1String>
#include <QStringList>

#include "aluchop/core/AppInfo.hpp"
#include "aluchop/core/Exceptions.hpp"
#include "aluchop/models/Order.hpp"

namespace aluchop::models {
namespace {

/// \brief Width of the fixed-pitch receipt column, in characters.
constexpr int kReceiptWidth = 46;

/// \brief Nepal's VAT rate, in whole percent.
///
/// It appears here only so the receipt can *disclose* how much tax is already
/// inside the price the guest is paying — the figure a VAT invoice is required to
/// show. It is never used to increase a total.
constexpr std::int64_t kVatPercent = 13;

/// \brief The tax already contained in a tax-inclusive amount.
/// \param inclusive A gross, tax-inclusive amount.
/// \return `inclusive - inclusive / 1.13`, computed in integer paisa.
///
/// The division is done as exact integer arithmetic with half-up rounding
/// (`net = round(gross * 100 / 113)`) so the two printed components always add
/// back up to the printed total — a receipt whose lines do not reconcile is a
/// receipt an auditor rejects.
core::Money taxContainedIn(core::Money inclusive) noexcept
{
    const std::int64_t gross = inclusive.paisa();
    if (gross <= 0)
        return core::Money::zero();

    const std::int64_t denom = 100 + kVatPercent;
    // round-half-up of (gross * 100) / denom, without ever touching a double
    const std::int64_t net = (gross * 200 + denom) / (2 * denom);
    return core::Money(gross - net);
}

/// \brief Lay a label out on the left and a figure hard right, within kReceiptWidth.
QString row(const QString& label, const QString& figure)
{
    const int room = kReceiptWidth - figure.size();
    if (room <= 1)
        return label + QLatin1Char(' ') + figure;

    QString left = label;
    if (left.size() > room - 1)
        left = left.left(room - 1);
    return left.leftJustified(room, QLatin1Char(' ')) + figure;
}

/// \brief Centre \p text in the receipt column.
QString centred(const QString& text)
{
    if (text.size() >= kReceiptWidth)
        return text;
    const int pad = (kReceiptWidth - text.size()) / 2;
    return QString(pad, QLatin1Char(' ')) + text;
}

/// \brief Human label for the till, as opposed to the database token.
QString methodLabel(PaymentMethod m)
{
    switch (m) {
    case PaymentMethod::Cash:
        return QStringLiteral("Cash");
    case PaymentMethod::Card:
        return QStringLiteral("Card");
    case PaymentMethod::Wallet:
        return QStringLiteral("Digital Wallet");
    }
    return QStringLiteral("Cash");
}

} // namespace

Bill::Bill(const Order& order)
    : m_orderId(order.id())
    , m_orderNumber(order.orderNumber())
    , m_items(order.items())      // the bill takes its OWN copy of the lines …
    , m_subtotal(order.subtotal())// … and freezes the figure they came to.
{
    // From here on the bill is independent of the order: editing the order, or
    // re-pricing the menu, cannot change a bill that has already been shown to a
    // guest.
}

void Bill::setDiscount(core::Money amount, const QString& label)
{
    if (m_settled)
        throw core::ValidationException(
            "Bill for order " + m_orderNumber.toStdString()
            + " is already settled and can no longer be discounted");

    if (amount.isNegative())
        throw core::ValidationException("Discount must not be negative");

    // A bill may reach zero but never go below it: a discount larger than the
    // basket would turn the till into a cash dispenser.
    if (amount > m_subtotal)
        throw core::ValidationException(
            "Discount " + amount.toString().toStdString() + " exceeds the subtotal "
            + m_subtotal.toString().toStdString());

    m_discount = amount;
    m_discountLabel = amount.isZero() ? QString() : label.trimmed();
}

void Bill::setServiceCharge(core::Money v)
{
    if (m_settled)
        throw core::ValidationException(
            "Bill for order " + m_orderNumber.toStdString()
            + " is already settled and can no longer be re-charged");

    if (v.isNegative())
        throw core::ValidationException("Service charge must not be negative");

    m_serviceCharge = v;
}

void Bill::settle(PaymentMethod m, core::Money tendered, core::Money change)
{
    // Private, and callable only by services::BillingService (the sole friend of
    // this class). That is what makes it impossible for a widget to mark a bill
    // paid without a payments row ever reaching the database.
    if (m_settled)
        throw core::ValidationException(
            "Bill for order " + m_orderNumber.toStdString() + " has already been settled");

    const core::Money due = total();
    if (tendered < due)
        throw core::ValidationException(
            "Tendered " + tendered.toString().toStdString() + " is less than the amount due "
            + due.toString().toStdString());
    if (change.isNegative())
        throw core::ValidationException("Change must not be negative");

    m_method = m;
    m_tendered = tendered;
    m_change = change;
    m_settled = true;
}

QString Bill::toPrintableText() const
{
    const QString heavy = QString(kReceiptWidth, QLatin1Char('='));
    const QString light = QString(kReceiptWidth, QLatin1Char('-'));

    const core::Money due = total();
    const core::Money vat = taxContainedIn(due);
    const core::Money taxable = due - vat;

    QStringList out;
    out << heavy;
    out << centred(QString::fromUtf8(core::kAppInfo.appName));
    out << centred(QStringLiteral("RECEIPT"));
    out << light;
    out << row(QStringLiteral("Order"), m_orderNumber.isEmpty() ? QStringLiteral("(unsaved)")
                                                                : m_orderNumber);
    out << light;

    if (m_items.empty()) {
        out << QStringLiteral("(no items)");
    } else {
        for (const OrderItem& line : m_items) {
            out << row(QStringLiteral("%1 x %2").arg(line.qty()).arg(line.name()),
                       line.lineTotal().toString());
            if (!line.note().isEmpty())
                out << QStringLiteral("    (%1)").arg(line.note());
        }
    }

    out << light;
    out << row(QStringLiteral("Subtotal"), m_subtotal.toString());

    if (!m_discount.isZero()) {
        QString label = QStringLiteral("Discount");
        if (!m_discountLabel.isEmpty())
            label += QStringLiteral(" (%1)").arg(m_discountLabel);
        // Rendered as a negative figure so the column visibly subtracts.
        out << row(label, (-m_discount).toString());
    }
    if (!m_promoCode.isEmpty())
        out << row(QStringLiteral("Promo code"), m_promoCode);
    if (!m_serviceCharge.isZero())
        out << row(QStringLiteral("Service charge"), m_serviceCharge.toString());

    out << heavy;
    out << row(QStringLiteral("TOTAL"), due.toString());
    out << heavy;

    // --- Tax disclosure -----------------------------------------------------
    // These two lines are a BREAKDOWN of the total above, not an addition to it.
    // taxable + VAT == TOTAL, exactly.
    out << row(QStringLiteral("Taxable amount"), taxable.toString());
    out << row(QStringLiteral("VAT %1% (included)").arg(kVatPercent), vat.toString());
    out << QStringLiteral("All prices are inclusive of tax.");

    if (m_settled) {
        out << light;
        out << row(QStringLiteral("Paid by"), methodLabel(m_method));
        out << row(QStringLiteral("Tendered"), m_tendered.toString());
        out << row(QStringLiteral("Change"), m_change.toString());
    } else {
        out << light;
        out << centred(QStringLiteral("** NOT YET PAID **"));
    }

    out << heavy;
    out << centred(QStringLiteral("Thank you — please come again!"));
    out << centred(QString::fromUtf8(core::kAppInfo.developer)
                   + QLatin1String(" (") + QString::fromUtf8(core::kAppInfo.rollNo)
                   + QLatin1Char(')'));
    out << heavy;

    return out.join(QLatin1Char('\n'));
}

/// @oop-concept Stream Insertion Operator :: a bill writes itself into any
/// std::ostream, which is what lets the raw <fstream> layer append a plain-text
/// receipt without knowing anything about Qt.
///
/// Defined at namespace scope to match the friend declaration inside Bill, so it
/// reads the private figures directly rather than through six getters.
std::ostream& operator<<(std::ostream& os, const Bill& b)
{
    os << "BILL " << (b.m_orderNumber.isEmpty() ? std::string("(unsaved)")
                                                : b.m_orderNumber.toStdString())
       << " [order id " << b.m_orderId << "]\n";

    for (const OrderItem& line : b.m_items) {
        os << "  " << line.qty() << " x " << line.name().toStdString() << "  "
           << line.lineTotal() << '\n';
    }

    os << "  SUBTOTAL       " << b.m_subtotal << '\n';
    if (!b.m_discount.isZero())
        os << "  DISCOUNT      -" << b.m_discount
           << (b.m_discountLabel.isEmpty() ? std::string()
                                           : "  (" + b.m_discountLabel.toStdString() + ")")
           << '\n';
    if (!b.m_promoCode.isEmpty())
        os << "  PROMO          " << b.m_promoCode.toStdString() << '\n';
    if (!b.m_serviceCharge.isZero())
        os << "  SERVICE        " << b.m_serviceCharge << '\n';

    const core::Money due = b.total();
    os << "  TOTAL          " << due << '\n';
    // Disclosure only — the total above already contains this tax.
    os << "  VAT " << kVatPercent << "% INCL  " << taxContainedIn(due) << '\n';

    if (b.m_settled) {
        os << "  PAID BY        " << methodLabel(b.m_method).toStdString() << '\n'
           << "  TENDERED       " << b.m_tendered << '\n'
           << "  CHANGE         " << b.m_change << '\n';
    } else {
        os << "  UNSETTLED\n";
    }

    os << "  All prices are inclusive of tax." << std::endl;
    return os;
}

} // namespace aluchop::models

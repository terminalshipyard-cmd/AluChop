#pragma once

/**
 * @file BillingDialog.hpp
 * @brief Bill breakdown, discounts and promos, payment, change and receipt.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QDialog>
#include <QString>

#include "aluchop/models/Bill.hpp"

namespace aluchop::services {
class AppContext;
}

class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace aluchop::gui {

/**
 * @brief Closing an order: what is owed, what was paid, what comes back.
 *
 * Opened from OrdersPage for a Served order. services::BillingService prepares a
 * models::Bill (line snapshot + subtotal + discount + service charge), the dialog displays it,
 * and only BillingService may settle it — the model declares that service a friend precisely
 * so that no widget can mark a bill paid behind the engine's back.
 *
 * @par Tax rule (SPEC §0, non-negotiable)
 * Menu prices are **already tax-inclusive**. This dialog shows Subtotal − Discount +
 * Service charge = Total and a line reading "All prices are inclusive of tax." No control on
 * this dialog can add tax on top of a price, because no such code path exists.
 *
 * Payment methods: Cash (tendered field enabled, change computed live through
 * BillingService::changeFor), Card and Digital Wallet (tendered locked to the exact total).
 * After settlement the receipt can be printed or written to PDF through gui::PdfExporter.
 *
 * @par Ownership
 * Stack- or heap-allocated with the OrdersPage as parent and exec()'d modally; the bill is a
 * **value member**, so the dialog owns a consistent snapshot for its whole lifetime.
 *
 * @oop-concept Objects as Members :: models::Bill is held by value, not by pointer — the
 *              dialog's state IS the prepared bill
 * @oop-concept Friend Class (surfaced) :: settlement is a BillingService privilege; this
 *              dialog can only ask
 */
class BillingDialog : public QDialog {
    Q_OBJECT
public:
    /// @param ctx composition root; uses AppContext::billing() and orders().
    /// @param orderId the Served order being billed.
    /// @param parent parent widget (OrdersPage), used for modality and centring.
    BillingDialog(services::AppContext& ctx, int orderId, QWidget* parent = nullptr);

private slots:
    /// Re-prepares the bill with the entered promo code; an invalid or expired code is shown
    /// inline and leaves the previous bill untouched.
    void onApplyPromo();

    /// Enables the tendered field for Cash and locks it to the total for Card/Wallet.
    void onMethodChanged();

    /// Recomputes the change label live from the tendered amount.
    void onTenderedEdited();

    /// Settles through BillingService (payment recorded, order closed, loyalty awarded,
    /// audit written), then shows the receipt preview.
    void onSettle();

    /// Sends the settled receipt to a printer via gui::PdfExporter::printReceipt().
    void onPrintReceipt();

private:
    /// Repaints every money label from m_bill; the single place that formats currency here,
    /// always through core::formatNpr.
    void updateTotals();

    services::AppContext& m_ctx;         ///< Composition root.
    int m_orderId;                       ///< Order being billed (immutable for the dialog's life).
    models::Bill m_bill;                 ///< Current prepared bill — a value, not a handle.
    QTableWidget* m_lines = nullptr;     ///< Item | Qty | Unit price | Line total.
    QLineEdit* m_promo = nullptr;        ///< Promo-code entry.
    QComboBox* m_method = nullptr;       ///< Cash | Card | Digital Wallet.
    QLineEdit* m_tendered = nullptr;     ///< Amount handed over (Cash only).
    QLabel* m_subtotal = nullptr;        ///< Sum of line totals.
    QLabel* m_discount = nullptr;        ///< Promo/staff/manual discount with its label.
    QLabel* m_serviceCharge = nullptr;   ///< Service charge, from settings.
    QLabel* m_total = nullptr;           ///< Subtotal − discount + service charge (tax inclusive).
    QLabel* m_change = nullptr;          ///< Tendered − total, never negative.
};

} // namespace aluchop::gui

/**
 * @file BillingDialog.cpp
 * @brief Bill breakdown, discounts and promos, payment, change and receipt.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * @par The tax rule (SPEC §0, non-negotiable)
 * Menu prices are **already tax-inclusive**. The amount owed is
 * `subtotal − discount + serviceCharge` and nothing else. The VAT line on this dialog is a
 * *reverse* computation — the tax already contained in the total — and is labelled as such.
 * There is no code path in this file that adds tax to anything.
 */

#include "aluchop/gui/BillingDialog.hpp"

#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <exception>

#include "aluchop/core/AppInfo.hpp"
#include "aluchop/core/Money.hpp"
#include "aluchop/core/Result.hpp"
#include "aluchop/gui/PdfExporter.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/gui/Widgets.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/OrderItem.hpp"
#include "aluchop/models/Payment.hpp"
#include "aluchop/services/AppContext.hpp"

namespace aluchop::gui {
namespace {

/// Nepal VAT rate. Used **only** to break an inclusive price back down for display.
constexpr int kVatPercent = 13;

QLabel* styledLabel(const QString& text, const QString& objectName, QWidget* parent,
                    int pointDelta = 0, QFont::Weight weight = QFont::Normal) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(objectName);
    QFont f = label->font();
    if (pointDelta != 0) f.setPointSize(std::max(8, f.pointSize() + pointDelta));
    f.setWeight(weight);
    label->setFont(f);
    return label;
}

QPushButton* makeButton(const QString& text, const QString& objectName, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    b->setObjectName(objectName);
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumHeight(38);
    return b;
}

/// Tags a widget for later lookup. A dynamic property is used rather than objectName because
/// objectName is ThemeManager's QSS selector — renaming a label would strip its styling.
void tagWidget(QWidget* w, const QString& name) { w->setProperty("aluchopTag", name); }

/// @oop-concept Function Template :: one lookup serves every tagged widget type in this dialog
template <typename T>
T findTagged(const QWidget* root, const QString& name) {
    const auto candidates = root->template findChildren<T>();
    for (T w : candidates)
        if (w->property("aluchopTag").toString() == name) return w;
    return nullptr;
}

/// Paints a label with an explicit palette colour (deltas, warnings, totals).
void tint(QLabel* label, const QColor& colour) {
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, colour);
    label->setPalette(p);
}

/**
 * @brief The VAT already contained in an inclusive amount.
 * @param gross the tax-inclusive amount actually payable.
 * @param pct the VAT rate the price includes.
 * @return `gross * pct / (100 + pct)`, rounded half-up, in integer paisa.
 *
 * This is a *breakdown*, never an addition: `net + vat == gross` by construction.
 */
core::Money inclusiveVat(core::Money gross, int pct) {
    const long long denominator = 100 + pct;
    const long long vat = (gross.paisa() * pct + denominator / 2) / denominator;
    return core::Money(vat);
}

/// "1250.00" — a plain, parseable amount with no currency prefix or grouping.
QString plainAmount(core::Money m) {
    const long long paisa = m.paisa() < 0 ? 0 : m.paisa();
    return QString::number(paisa / 100) + QLatin1Char('.') +
           QString::number(paisa % 100).rightJustified(2, QLatin1Char('0'));
}

/**
 * @brief Parses "1,250.50" / "Rs 1250" into core::Money without ever touching a double.
 * @return false when the text is not a well-formed, non-negative amount.
 */
bool parseNpr(const QString& raw, core::Money& out) {
    QString text = raw.trimmed();
    text.remove(QStringLiteral("Rs"), Qt::CaseInsensitive);
    text.remove(QLatin1Char(','));
    text.remove(QLatin1Char(' '));
    if (text.isEmpty() || text.startsWith(QLatin1Char('-'))) return false;

    const QStringList parts = text.split(QLatin1Char('.'));
    if (parts.size() > 2) return false;

    bool ok = false;
    const qlonglong rupees = parts.at(0).isEmpty() ? 0 : parts.at(0).toLongLong(&ok);
    if (!parts.at(0).isEmpty() && !ok) return false;

    qlonglong paisa = 0;
    if (parts.size() == 2 && !parts.at(1).isEmpty()) {
        QString frac = parts.at(1);
        if (frac.size() > 2) return false;
        while (frac.size() < 2) frac.append(QLatin1Char('0'));
        paisa = frac.toLongLong(&ok);
        if (!ok) return false;
    }
    out = core::Money::fromRupees(rupees, paisa);
    return true;
}

/// Adds one caption/value pair to the breakdown grid and returns the value label.
/// Captions are muted, values carry the normal text colour — that hierarchy is the whole point.
QLabel* addBreakdownRow(QGridLayout* grid, int row, const QString& caption,
                        const QString& captionTag, const QString& valueTag, QWidget* parent,
                        int pointDelta = 0, QFont::Weight weight = QFont::Normal) {
    auto* captionLabel = styledLabel(caption, QStringLiteral("mutedLabel"), parent, pointDelta);
    if (!captionTag.isEmpty()) tagWidget(captionLabel, captionTag);
    auto* valueLabel = styledLabel(QStringLiteral("—"), QString(), parent, pointDelta, weight);
    if (!valueTag.isEmpty()) tagWidget(valueLabel, valueTag);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(captionLabel, row, 0);
    grid->addWidget(valueLabel, row, 1);
    return valueLabel;
}

/// Recomputes only the change line and the Settle button's enabled state.
void refreshChange(services::AppContext& ctx, const models::Bill& bill, QComboBox* method,
                   QLineEdit* tendered, QLabel* change, QPushButton* settle) {
    const Palette& pal = ThemeManager::instance().palette();
    const core::Money total = bill.total();
    const bool isCash =
        static_cast<models::PaymentMethod>(method->currentData().toInt()) ==
        models::PaymentMethod::Cash;

    if (bill.isSettled()) {
        change->setText(core::formatNpr(bill.change()));
        tint(change, pal.success);
        if (settle) settle->setEnabled(false);
        return;
    }

    core::Money handed;
    if (!isCash) {
        handed = total;
    } else if (!parseNpr(tendered->text(), handed)) {
        change->setText(QStringLiteral("—"));
        tint(change, pal.textMuted);
        if (settle) settle->setEnabled(false);
        return;
    }

    if (handed < total) {
        change->setText(QStringLiteral("%1 short").arg(core::formatNpr(total - handed)));
        tint(change, pal.danger);
        if (settle) settle->setEnabled(false);
        return;
    }

    // changeFor() throws below the total, which the guard above has already excluded.
    const core::Money due = ctx.billing().changeFor(total, handed);
    change->setText(core::formatNpr(due));
    tint(change, due.isZero() ? pal.textMuted : pal.success);
    if (settle) settle->setEnabled(true);
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

BillingDialog::BillingDialog(services::AppContext& ctx, int orderId, QWidget* parent)
    : QDialog(parent), m_ctx(ctx), m_orderId(orderId) {
    setObjectName(QStringLiteral("billingDialog"));
    setWindowTitle(QStringLiteral("Settle bill"));
    setModal(true);
    setMinimumWidth(640);
    setMinimumHeight(620);

    const Palette& pal = ThemeManager::instance().palette();
    const int serviceChargePct = std::clamp(
        m_ctx.settings().get(QStringLiteral("billing.service_charge_pct"),
                             QStringLiteral("10")).toInt(),
        0, 100);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(16);

    // --- header ------------------------------------------------------------
    auto* headings = new QVBoxLayout();
    headings->setSpacing(2);
    headings->addWidget(styledLabel(QStringLiteral("Settle bill"), QStringLiteral("pageTitle"),
                                    this, 8, QFont::DemiBold));
    auto* orderLabel = styledLabel(QStringLiteral("Preparing…"), QStringLiteral("mutedLabel"), this);
    tagWidget(orderLabel, QStringLiteral("billOrderLabel"));
    headings->addWidget(orderLabel);
    root->addLayout(headings);

    // --- lines / receipt ---------------------------------------------------
    auto* linesPanel = new GlassPanel(this);
    linesPanel->setObjectName(QStringLiteral("card"));
    auto* linesColumn = new QVBoxLayout(linesPanel);
    linesColumn->setContentsMargins(16, 14, 16, 14);
    linesColumn->setSpacing(10);

    auto* stack = new QStackedWidget(linesPanel);
    stack->setObjectName(QStringLiteral("billStack"));

    m_lines = new ElegantTable(QStringList{QStringLiteral("Item"), QStringLiteral("Qty"),
                                           QStringLiteral("Unit price"), QStringLiteral("Line total")},
                               stack);
    m_lines->setSortingEnabled(false);
    m_lines->setMinimumHeight(180);
    m_lines->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_lines->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_lines->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_lines->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    stack->addWidget(m_lines);

    auto* receipt = new QTextEdit(stack);
    receipt->setObjectName(QStringLiteral("receiptView"));
    receipt->setReadOnly(true);
    receipt->setFrameShape(QFrame::NoFrame);
    receipt->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    stack->addWidget(receipt);

    linesColumn->addWidget(stack, 1);
    root->addWidget(linesPanel, 1);

    // --- money breakdown ---------------------------------------------------
    auto* totalsPanel = new GlassPanel(this);
    totalsPanel->setObjectName(QStringLiteral("card"));
    auto* totalsColumn = new QVBoxLayout(totalsPanel);
    totalsColumn->setContentsMargins(18, 16, 18, 16);
    totalsColumn->setSpacing(8);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(0, 1);

    m_subtotal = addBreakdownRow(grid, 0, QStringLiteral("Subtotal"), QString(), QString(),
                                 totalsPanel);
    // A *breakdown* of the tax already inside the price — never an addition to it.
    QLabel* vat = addBreakdownRow(
        grid, 1, QStringLiteral("of which VAT (%1%) — incl. VAT, never added").arg(kVatPercent),
        QString(), QStringLiteral("billVat"), totalsPanel, -1);
    m_discount = addBreakdownRow(grid, 2, QStringLiteral("Discount"),
                                 QStringLiteral("billDiscountCaption"), QString(), totalsPanel);
    m_serviceCharge = addBreakdownRow(
        grid, 3, QStringLiteral("Service charge (%1%)").arg(serviceChargePct), QString(),
        QString(), totalsPanel);

    auto* rule = new QFrame(totalsPanel);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Plain);
    rule->setFixedHeight(1);
    grid->addWidget(rule, 4, 0, 1, 2);

    m_total = addBreakdownRow(grid, 5, QStringLiteral("Total payable"), QString(), QString(),
                              totalsPanel, 6, QFont::DemiBold);
    tint(m_total, pal.primary);
    m_change = addBreakdownRow(grid, 6, QStringLiteral("Change"), QString(), QString(),
                               totalsPanel, 1, QFont::DemiBold);
    totalsColumn->addLayout(grid);

    auto* taxNote = styledLabel(QStringLiteral("All prices are inclusive of tax."),
                                QStringLiteral("mutedLabel"), totalsPanel, -1);
    totalsColumn->addWidget(taxNote);
    Q_UNUSED(vat)
    root->addWidget(totalsPanel);

    // --- promo -------------------------------------------------------------
    auto* promoRow = new QHBoxLayout();
    promoRow->setSpacing(10);
    m_promo = new SearchBar(QStringLiteral("Promo code, e.g. DASHAIN10"), this);
    m_promo->setClearButtonEnabled(true);
    auto* applyBtn = makeButton(QStringLiteral("Apply"), QStringLiteral("ghostButton"), this);
    applyBtn->setObjectName(QStringLiteral("ghostButton"));
    connect(applyBtn, &QPushButton::clicked, this, &BillingDialog::onApplyPromo);
    connect(m_promo, &QLineEdit::returnPressed, this, &BillingDialog::onApplyPromo);
    promoRow->addWidget(m_promo, 1);
    promoRow->addWidget(applyBtn, 0);
    root->addLayout(promoRow);

    auto* promoStatus = styledLabel(QString(), QStringLiteral("mutedLabel"), this, -1);
    tagWidget(promoStatus, QStringLiteral("promoStatus"));
    promoStatus->setWordWrap(true);
    promoStatus->setVisible(false);
    root->addWidget(promoStatus);

    // --- payment -----------------------------------------------------------
    auto* payRow = new QHBoxLayout();
    payRow->setSpacing(10);

    m_method = new QComboBox(this);
    m_method->setMinimumHeight(38);
    m_method->setCursor(Qt::PointingHandCursor);
    m_method->addItem(QStringLiteral("Cash"), static_cast<int>(models::PaymentMethod::Cash));
    m_method->addItem(QStringLiteral("Card"), static_cast<int>(models::PaymentMethod::Card));
    m_method->addItem(QStringLiteral("Digital Wallet"),
                      static_cast<int>(models::PaymentMethod::Wallet));

    m_tendered = new QLineEdit(this);
    m_tendered->setMinimumHeight(38);
    m_tendered->setPlaceholderText(QStringLiteral("Amount handed over"));
    m_tendered->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    payRow->addWidget(styledLabel(QStringLiteral("Payment"), QStringLiteral("mutedLabel"), this), 0);
    payRow->addWidget(m_method, 1);
    payRow->addWidget(styledLabel(QStringLiteral("Tendered"), QStringLiteral("mutedLabel"), this), 0);
    payRow->addWidget(m_tendered, 1);
    root->addLayout(payRow);

    auto* error = styledLabel(QString(), QStringLiteral("mutedLabel"), this, -1);
    tagWidget(error, QStringLiteral("billError"));
    error->setWordWrap(true);
    error->setVisible(false);
    tint(error, pal.danger);
    root->addWidget(error);

    // --- actions -----------------------------------------------------------
    auto* actions = new QHBoxLayout();
    actions->setSpacing(10);

    auto* printBtn = makeButton(QStringLiteral("Print receipt"), QStringLiteral("ghostButton"), this);
    printBtn->setProperty("receiptAction", true);  // found again once the bill is settled
    printBtn->setEnabled(false);
    connect(printBtn, &QPushButton::clicked, this, &BillingDialog::onPrintReceipt);

    auto* pdfBtn = makeButton(QStringLiteral("Save as PDF"), QStringLiteral("ghostButton"), this);
    pdfBtn->setProperty("receiptAction", true);
    pdfBtn->setEnabled(false);
    connect(pdfBtn, &QPushButton::clicked, this, [this]() {
        QDir reports(QStringLiteral("reports"));
        if (!reports.exists()) reports.mkpath(QStringLiteral("."));
        const QString path = reports.filePath(
            QStringLiteral("receipt-%1.pdf")
                .arg(m_bill.orderNumber().isEmpty() ? QString::number(m_orderId)
                                                    : m_bill.orderNumber()));
        const core::Result<QString> written = PdfExporter::receiptPdf(m_bill, path);
        if (written.isErr()) {
            m_ctx.notifications().notify(QStringLiteral("PDF export failed"), written.error(), 3);
        } else {
            m_ctx.notifications().notify(QStringLiteral("Receipt saved"), written.value(), 1);
        }
    });

    auto* closeBtn = makeButton(QStringLiteral("Close"), QStringLiteral("ghostButton"), this);
    closeBtn->setObjectName(QStringLiteral("ghostButton"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto* settleBtn = makeButton(QStringLiteral("Take payment"), QStringLiteral("primaryButton"),
                                 this);
    tagWidget(settleBtn, QStringLiteral("settleButton"));
    settleBtn->setMinimumWidth(160);
    connect(settleBtn, &QPushButton::clicked, this, &BillingDialog::onSettle);

    actions->addWidget(printBtn);
    actions->addWidget(pdfBtn);
    actions->addStretch(1);
    actions->addWidget(closeBtn);
    actions->addWidget(settleBtn);
    root->addLayout(actions);

    connect(m_method, &QComboBox::currentIndexChanged, this, &BillingDialog::onMethodChanged);
    connect(m_tendered, &QLineEdit::textEdited, this, &BillingDialog::onTenderedEdited);

    // --- prepare the bill ---------------------------------------------------
    const core::Result<models::Bill> prepared =
        m_ctx.billing().prepareBill(m_orderId, QString(), serviceChargePct);
    if (prepared.isErr()) {
        error->setText(prepared.error());
        error->setVisible(true);
        orderLabel->setText(QStringLiteral("This order cannot be billed."));
        settleBtn->setEnabled(false);
        m_promo->setEnabled(false);
        m_method->setEnabled(false);
        m_tendered->setEnabled(false);
        return;
    }

    m_bill = prepared.value();
    orderLabel->setText(QStringLiteral("Order %1  ·  %2 line%3")
                            .arg(m_bill.orderNumber())
                            .arg(m_bill.items().size())
                            .arg(m_bill.items().size() == 1 ? QString() : QStringLiteral("s")));
    m_tendered->setText(plainAmount(m_bill.total()));
    updateTotals();
    m_tendered->setFocus();
    m_tendered->selectAll();
}

// ---------------------------------------------------------------------------
// Repainting
// ---------------------------------------------------------------------------

void BillingDialog::updateTotals() {
    const Palette& pal = ThemeManager::instance().palette();

    // --- lines --------------------------------------------------------------
    m_lines->setRowCount(0);
    int row = 0;
    for (const models::OrderItem& line : m_bill.items()) {
        m_lines->insertRow(row);

        auto* nameCell = new QTableWidgetItem(line.name());
        QFont nf = nameCell->font();
        nf.setWeight(QFont::DemiBold);
        nameCell->setFont(nf);
        if (!line.note().isEmpty()) nameCell->setToolTip(line.note());
        m_lines->setItem(row, 0, nameCell);

        auto* qtyCell = new QTableWidgetItem(QString::number(line.qty()));
        qtyCell->setTextAlignment(Qt::AlignCenter);
        m_lines->setItem(row, 1, qtyCell);

        auto* unitCell = new QTableWidgetItem(core::formatNpr(line.unitPrice()));
        unitCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        unitCell->setForeground(pal.textMuted);
        m_lines->setItem(row, 2, unitCell);

        auto* totalCell = new QTableWidgetItem(core::formatNpr(line.lineTotal()));
        totalCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_lines->setItem(row, 3, totalCell);

        ++row;
    }

    // --- money --------------------------------------------------------------
    const core::Money total = m_bill.total();
    m_subtotal->setText(core::formatNpr(m_bill.subtotal()));

    if (auto* vat = findTagged<QLabel*>(this, QStringLiteral("billVat"))) {
        vat->setText(core::formatNpr(inclusiveVat(total, kVatPercent)));
        tint(vat, pal.textMuted);
    }

    if (auto* caption = findTagged<QLabel*>(this, QStringLiteral("billDiscountCaption"))) {
        caption->setText(m_bill.discountLabel().isEmpty()
                             ? QStringLiteral("Discount")
                             : QStringLiteral("Discount — %1").arg(m_bill.discountLabel()));
    }
    if (m_bill.discount().isZero()) {
        m_discount->setText(QStringLiteral("—"));
        tint(m_discount, pal.textMuted);
    } else {
        m_discount->setText(QStringLiteral("− %1").arg(core::formatNpr(m_bill.discount())));
        tint(m_discount, pal.success);
    }

    m_serviceCharge->setText(m_bill.serviceCharge().isZero()
                                 ? QStringLiteral("—")
                                 : core::formatNpr(m_bill.serviceCharge()));
    m_total->setText(core::formatNpr(total));
    tint(m_total, pal.primary);

    // Card and wallet are always tendered exactly; cash is the only method with change.
    onMethodChanged();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void BillingDialog::onApplyPromo() {
    if (m_bill.isSettled()) return;

    const Palette& pal = ThemeManager::instance().palette();
    auto* status = findTagged<QLabel*>(this, QStringLiteral("promoStatus"));
    const QString code = m_promo->text().trimmed().toUpper();
    const int serviceChargePct = std::clamp(
        m_ctx.settings().get(QStringLiteral("billing.service_charge_pct"),
                             QStringLiteral("10")).toInt(),
        0, 100);

    const core::Result<models::Bill> prepared =
        m_ctx.billing().prepareBill(m_orderId, code, serviceChargePct);
    if (prepared.isErr()) {
        if (status) {
            status->setText(prepared.error());
            tint(status, pal.danger);
            status->setVisible(true);
        }
        return;
    }

    const models::Bill& candidate = prepared.value();
    if (!code.isEmpty() && candidate.promoCode().isEmpty()) {
        // An invalid, expired or under-minimum code leaves the prepared bill untouched.
        if (status) {
            status->setText(QStringLiteral("“%1” cannot be used on this bill.").arg(code));
            tint(status, pal.danger);
            status->setVisible(true);
        }
        return;
    }

    m_bill = candidate;
    if (status) {
        if (code.isEmpty()) {
            status->setText(QStringLiteral("Promo code cleared."));
            tint(status, pal.textMuted);
        } else {
            status->setText(QStringLiteral("“%1” applied — %2 off.")
                                .arg(code, core::formatNpr(m_bill.discount())));
            tint(status, pal.success);
        }
        status->setVisible(true);
    }
    m_tendered->setText(plainAmount(m_bill.total()));
    updateTotals();
}

void BillingDialog::onMethodChanged() {
    const auto method = static_cast<models::PaymentMethod>(m_method->currentData().toInt());
    const bool isCash = method == models::PaymentMethod::Cash;

    m_tendered->setReadOnly(!isCash);
    m_tendered->setEnabled(!m_bill.isSettled());
    if (!isCash) m_tendered->setText(plainAmount(m_bill.total()));
    m_tendered->setToolTip(isCash
                               ? QStringLiteral("Type what the guest handed over.")
                               : QStringLiteral("Card and wallet payments are tendered exactly."));

    refreshChange(m_ctx, m_bill, m_method, m_tendered, m_change,
                  findTagged<QPushButton*>(this, QStringLiteral("settleButton")));
}

void BillingDialog::onTenderedEdited() {
    refreshChange(m_ctx, m_bill, m_method, m_tendered, m_change,
                  findTagged<QPushButton*>(this, QStringLiteral("settleButton")));
}

void BillingDialog::onSettle() {
    if (m_bill.isSettled()) return;

    const Palette& pal = ThemeManager::instance().palette();
    auto* error = findTagged<QLabel*>(this, QStringLiteral("billError"));
    auto* settleBtn = findTagged<QPushButton*>(this, QStringLiteral("settleButton"));

    const auto method = static_cast<models::PaymentMethod>(m_method->currentData().toInt());
    core::Money tendered = m_bill.total();
    if (method == models::PaymentMethod::Cash && !parseNpr(m_tendered->text(), tendered)) {
        if (error) {
            error->setText(QStringLiteral("Enter the tendered amount, e.g. 1500 or 1500.50."));
            error->setVisible(true);
        }
        m_tendered->setFocus();
        return;
    }
    if (tendered < m_bill.total()) {
        // The dialog may never accept less than the total — the guard is here as well as in the
        // service, so no UI state can even reach settle() with a short tender.
        if (error) {
            error->setText(QStringLiteral("That is %1 short of the total.")
                               .arg(core::formatNpr(m_bill.total() - tendered)));
            error->setVisible(true);
        }
        m_tendered->setFocus();
        return;
    }

    const int cashierId = m_ctx.auth().currentUser() ? m_ctx.auth().currentUser()->id() : 0;
    const core::Result<models::Payment> paid =
        m_ctx.billing().settle(m_orderId, m_bill, method, tendered, cashierId);
    if (paid.isErr()) {
        if (error) {
            error->setText(paid.error());
            error->setVisible(true);
        }
        m_ctx.notifications().notify(QStringLiteral("Payment failed"), paid.error(), 3);
        return;
    }

    if (error) error->setVisible(false);

    // --- settled: show the receipt and lock the money controls ---------------
    if (auto* receipt = findChild<QTextEdit*>(QStringLiteral("receiptView")))
        receipt->setPlainText(m_ctx.billing().receiptText(m_bill));
    if (auto* stack = findChild<QStackedWidget*>(QStringLiteral("billStack")))
        stack->setCurrentIndex(1);
    if (auto* orderLabel = findTagged<QLabel*>(this, QStringLiteral("billOrderLabel"))) {
        orderLabel->setText(QStringLiteral("Order %1 · settled").arg(m_bill.orderNumber()));
        tint(orderLabel, pal.success);
    }

    m_promo->setEnabled(false);
    m_method->setEnabled(false);
    m_tendered->setReadOnly(true);
    if (settleBtn) {
        settleBtn->setEnabled(false);
        settleBtn->setText(QStringLiteral("Paid"));
    }
    for (QPushButton* button : findChildren<QPushButton*>()) {
        if (button->property("receiptAction").toBool()) button->setEnabled(true);
    }

    m_change->setText(core::formatNpr(m_bill.change()));
    tint(m_change, pal.success);

    m_ctx.notifications().notify(
        QStringLiteral("Payment taken"),
        QStringLiteral("Order %1 settled — %2 received, %3 change.")
            .arg(m_bill.orderNumber(), core::formatNpr(m_bill.tendered()),
                 core::formatNpr(m_bill.change())),
        1);
}

void BillingDialog::onPrintReceipt() {
    if (!m_bill.isSettled()) {
        m_ctx.notifications().notify(QStringLiteral("Receipt"),
                                     QStringLiteral("Take the payment first."), 0);
        return;
    }
    PdfExporter::printReceipt(m_bill, this);
}

} // namespace aluchop::gui

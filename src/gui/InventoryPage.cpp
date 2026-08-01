/**
 * @file InventoryPage.cpp
 * @brief Screen 6 — ingredients, stock levels, expiry, suppliers and restocking.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * services::InventoryService owns every rule on this screen — what counts as low stock, what
 * counts as expiring, how a movement is journalled — and the screen only displays and requests.
 * Automatic deduction when an order is served happens inside the service, driven by each menu
 * item's recipe, which is why nothing here knows anything about orders.
 *
 * Restocking goes through services::AdjustStockCommand on the shared CommandStack, so a
 * fat-fingered "+100 kg" is undone with Ctrl+Z and the reversal lands in the ledger like any
 * other movement. CommandStack::run() turns a thrown core::InventoryException into a
 * core::Result error, which arrives here as a red toast rather than as a crash.
 */

#include "aluchop/gui/InventoryPage.hpp"

#include <algorithm>
#include <memory>
#include <tuple>
#include <vector>

#include <QAbstractAnimation>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEasingCurve>
#include <QEvent>
#include <QFont>
#include <QFormLayout>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Money.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/gui/Widgets.hpp"
#include "aluchop/models/Ingredient.hpp"
#include "aluchop/models/Supplier.hpp"
#include "aluchop/services/AppContext.hpp"
#include "aluchop/services/Commands.hpp"

namespace aluchop::gui {
namespace {

using core::Money;

// --- house measurements, identical across the four management screens ------------------------
constexpr int kPageMargin = 24;
constexpr int kPageSpacing = 18;
constexpr int kPanelPad = 18;
constexpr int kControlH = 38;
constexpr int kFadeMs = 220;
constexpr int kExpiryHorizonDays = 7;  ///< matches InventoryService::expiring()'s default

/// @return the live palette; every colour on this screen is read from here, never literal hex.
const Palette& theme() { return ThemeManager::instance().palette(); }

/// @brief Builds a themed label.
/// @oop-concept Default Arguments :: most labels want the inherited size and weight
QLabel* makeLabel(const QString& text, const QString& role, QWidget* parent,
                  int pointSize = 0, QFont::Weight weight = QFont::Normal) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(role);
    QFont f = label->font();
    if (pointSize > 0) f.setPointSize(pointSize);
    f.setWeight(weight);
    label->setFont(f);
    return label;
}

/// @brief Builds a themed button (`primaryButton` / `ghostButton` / `dangerButton`).
QPushButton* makeButton(const QString& text, const QString& variant, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(variant);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(kControlH);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}

/// @brief Rounded "pill" label.
QLabel* makeChip(const QString& text, QWidget* parent) {
    auto* chip = new QLabel(text, parent);
    chip->setAlignment(Qt::AlignCenter);
    QFont f = chip->font();
    f.setPointSize(11);
    f.setWeight(QFont::DemiBold);
    chip->setFont(f);
    chip->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return chip;
}

/// @brief Repaints a chip from the live palette.
void styleChip(QLabel* chip, const QColor& accentColour) {
    chip->setStyleSheet(QStringLiteral("padding: 5px 14px; border-radius: 13px;"
                                       "background-color: rgba(%1,%2,%3,18%);"
                                       "color: %4;")
                            .arg(accentColour.red())
                            .arg(accentColour.green())
                            .arg(accentColour.blue())
                            .arg(accentColour.name()));
}

/// @brief Table cell that sorts on a numeric key while displaying formatted text.
///
/// @oop-concept Method Overriding :: QTableWidgetItem::operator< is overridden so that
///              "12.50 kg" sorts above "9.00 kg" instead of sorting as text
class SortableCell : public QTableWidgetItem {
public:
    SortableCell(const QString& text, double key) : QTableWidgetItem(text), m_key(key) {}

    bool operator<(const QTableWidgetItem& other) const override {
        const auto* rhs = dynamic_cast<const SortableCell*>(&other);
        return rhs ? m_key < rhs->m_key : QTableWidgetItem::operator<(other);
    }

private:
    double m_key;
};

/// @brief Plain, read-only text cell.
QTableWidgetItem* textCell(const QString& text,
                           Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
    auto* cell = new QTableWidgetItem(text);
    cell->setTextAlignment(align);
    return cell;
}

/// @brief Right-aligned numeric cell with tabular figures.
QTableWidgetItem* numberCell(const QString& text, double key) {
    auto* cell = new SortableCell(text, key);
    cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont f = cell->font();
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    f.setFeature(QFont::Tag("tnum"), 1);
#endif
    cell->setFont(f);
    return cell;
}

/// @brief Plays a one-off fade the first time its target screen is shown.
class EntranceAnimator : public QObject {
public:
    explicit EntranceAnimator(QWidget* target) : QObject(target), m_target(target) {
        target->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (!m_played && watched == m_target && event->type() == QEvent::Show) {
            m_played = true;
            auto* effect = new QGraphicsOpacityEffect(m_target);
            m_target->setGraphicsEffect(effect);
            auto* fade = new QPropertyAnimation(effect, "opacity", m_target);
            fade->setDuration(kFadeMs);
            fade->setStartValue(0.0);
            fade->setEndValue(1.0);
            fade->setEasingCurve(QEasingCurve::InOutCubic);
            QWidget* target = m_target;
            QObject::connect(fade, &QPropertyAnimation::finished, target, [target]() {
                QTimer::singleShot(0, target, [target]() { target->setGraphicsEffect(nullptr); });
            });
            fade->start(QAbstractAnimation::DeleteWhenStopped);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget* m_target;
    bool m_played = false;
};

void beginRepopulate(QTableWidget* table) {
    table->setSortingEnabled(false);
    table->clearContents();
    table->setRowCount(0);
}

void endRepopulate(QTableWidget* table) { table->setSortingEnabled(true); }

/// @brief Marks a screen as "repopulating" for as long as it is in scope.
///
/// @oop-concept Destructor / RAII :: the flag is cleared by scope exit, including the path where
///              an exception unwinds out of the middle of a repopulation
class BusyGuard {
public:
    explicit BusyGuard(QObject* screen) : m_screen(screen) {
        m_screen->setProperty("aluchopBusy", true);
    }
    ~BusyGuard() { m_screen->setProperty("aluchopBusy", false); }

    BusyGuard(const BusyGuard&) = delete;
    BusyGuard& operator=(const BusyGuard&) = delete;

    static bool busy(const QObject* screen) {
        return screen->property("aluchopBusy").toBool();
    }

private:
    QObject* m_screen;
};

/// @brief The four states a stock line can be in, in order of urgency.
/// @oop-concept Enumerations :: the shelf has a closed vocabulary of conditions, not free strings
enum class StockState { Expired, Low, ExpiringSoon, Healthy };

/// @return the urgency of one stock line. Low stock and expiry are the model's own predicates
///         (models::Ingredient::isLow / expiresWithin); this function only orders them.
StockState stateOf(const models::Ingredient& item) {
    const QDate expiry = item.expiryDate();
    if (expiry.isValid() && expiry < QDate::currentDate()) return StockState::Expired;
    if (item.isLow()) return StockState::Low;
    if (item.expiresWithin(kExpiryHorizonDays)) return StockState::ExpiringSoon;
    return StockState::Healthy;
}

/// @return the palette colour that carries the meaning of @p state — never a literal hex value,
///         so Light and Dark both stay legible.
QColor colourFor(StockState state) {
    const Palette& p = theme();
    switch (state) {
        case StockState::Expired:
        case StockState::Low:
            return p.danger;
        case StockState::ExpiringSoon:
            return p.secondary;
        case StockState::Healthy:
            break;
    }
    return p.success;
}

/// @return the human label for a stock state.
QString labelFor(StockState state) {
    switch (state) {
        case StockState::Expired:
            return QStringLiteral("Expired");
        case StockState::Low:
            return QStringLiteral("Low stock");
        case StockState::ExpiringSoon:
            return QStringLiteral("Expiring soon");
        case StockState::Healthy:
            break;
    }
    return QStringLiteral("In stock");
}

/// @brief Eight-segment gauge of stock against its reorder level.
/// @param stock current quantity.
/// @param threshold reorder level; a full bar is twice the reorder level.
/// @return e.g. `▰▰▰▰▰▱▱▱`.
QString levelGauge(double stock, double threshold) {
    constexpr int kSegments = 8;
    const double full = threshold > 0.0 ? threshold * 2.0 : std::max(stock, 1.0);
    double ratio = full > 0.0 ? stock / full : 0.0;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    const int filled = static_cast<int>(ratio * kSegments + 0.5);
    QString bar;
    for (int i = 0; i < kSegments; ++i) {
        bar += (i < filled) ? QStringLiteral("▰") : QStringLiteral("▱");
    }
    return bar;
}

/// @return the fraction of a full shelf @p stock represents, used as the gauge's sort key.
double levelRatio(double stock, double threshold) {
    const double full = threshold > 0.0 ? threshold * 2.0 : std::max(stock, 1.0);
    return full > 0.0 ? stock / full : 0.0;
}

/// @brief Formats a physical quantity. Quantities are `double` on purpose — they are kitchen
///        measurements, not money; money on this screen is always core::Money.
QString formatQty(double qty) { return QString::number(qty, 'f', 2); }

/// @brief A rupees + paisa pair, so currency is entered as integers and never touches a double.
struct MoneyField {
    QWidget* widget = nullptr;  ///< the composed row, ready for QFormLayout::addRow
    QSpinBox* rupees = nullptr; ///< whole rupees
    QSpinBox* paisa = nullptr;  ///< 0..99

    /// @return the entered amount in exact paisa.
    Money value() const {
        return Money::fromRupees(rupees ? rupees->value() : 0, paisa ? paisa->value() : 0);
    }
};

/// @brief Builds a rupees+paisa editor pre-filled with @p initial.
MoneyField makeMoneyField(QWidget* parent, Money initial) {
    MoneyField field;
    field.widget = new QWidget(parent);
    auto* row = new QHBoxLayout(field.widget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    field.rupees = new QSpinBox(field.widget);
    field.rupees->setRange(0, 100000000);
    field.rupees->setPrefix(QStringLiteral("Rs "));
    field.rupees->setGroupSeparatorShown(true);
    field.rupees->setValue(static_cast<int>(initial.wholeRupees()));

    field.paisa = new QSpinBox(field.widget);
    field.paisa->setRange(0, 99);
    field.paisa->setSuffix(QStringLiteral(" p"));
    field.paisa->setValue(static_cast<int>(initial.paisa() % 100));

    row->addWidget(field.rupees, 2);
    row->addWidget(field.paisa, 1);
    return field;
}

}  // namespace

// ============================================================================================
//  Construction
// ============================================================================================

InventoryPage::InventoryPage(services::AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    setObjectName(QStringLiteral("inventoryPage"));

    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    page->setSpacing(kPageSpacing);

    // --- header band ------------------------------------------------------------------------
    auto* header = new QHBoxLayout;
    header->setSpacing(12);

    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    titles->addWidget(makeLabel(pageTitle(), QStringLiteral("pageTitle"), this, 24, QFont::Bold));
    auto* subtitle = makeLabel(QStringLiteral("Reading the store room…"),
                               QStringLiteral("mutedLabel"), this, 11);
    subtitle->setProperty("aluchopRole", QStringLiteral("inventorySubtitle"));
    titles->addWidget(subtitle);
    header->addLayout(titles);
    header->addStretch(1);

    auto* addIngredientBtn = makeButton(QStringLiteral("＋  Ingredient"),
                                        QStringLiteral("ghostButton"), this);
    auto* editIngredientBtn = makeButton(QStringLiteral("Edit"), QStringLiteral("ghostButton"),
                                         this);
    auto* wasteBtn = makeButton(QStringLiteral("Record waste"), QStringLiteral("dangerButton"),
                                this);
    auto* restockBtn = makeButton(QStringLiteral("Restock"), QStringLiteral("primaryButton"),
                                  this);
    header->addWidget(addIngredientBtn);
    header->addWidget(editIngredientBtn);
    header->addWidget(wasteBtn);
    header->addWidget(restockBtn);
    page->addLayout(header);

    // --- body -------------------------------------------------------------------------------
    auto* body = new QHBoxLayout;
    body->setSpacing(kPageSpacing);

    // Left column: the shelf, then the ledger of the selected line -------------------------
    auto* leftColumn = new QVBoxLayout;
    leftColumn->setSpacing(kPageSpacing);

    auto* shelfPanel = new GlassPanel(this);
    auto* shelfCol = new QVBoxLayout(shelfPanel);
    shelfCol->setContentsMargins(kPanelPad, kPanelPad, kPanelPad, kPanelPad);
    shelfCol->setSpacing(12);

    auto* shelfHead = new QHBoxLayout;
    shelfHead->setSpacing(10);
    shelfHead->addWidget(makeLabel(QStringLiteral("Store room"), QStringLiteral("sectionTitle"),
                                   shelfPanel, 13, QFont::DemiBold));
    shelfHead->addStretch(1);
    auto* lowChip = makeChip(QStringLiteral("—"), shelfPanel);
    lowChip->setObjectName(QStringLiteral("inventoryLowChip"));
    auto* expiryChip = makeChip(QStringLiteral("—"), shelfPanel);
    expiryChip->setObjectName(QStringLiteral("inventoryExpiryChip"));
    shelfHead->addWidget(lowChip);
    shelfHead->addWidget(expiryChip);
    shelfCol->addLayout(shelfHead);

    auto* search = new SearchBar(QStringLiteral("Filter ingredients by name, unit or supplier…"),
                                 shelfPanel);
    search->setObjectName(QStringLiteral("searchBar"));
    search->setProperty("aluchopRole", QStringLiteral("inventorySearch"));
    search->setMinimumHeight(kControlH);
    shelfCol->addWidget(search);

    m_ingredients = new ElegantTable(
        QStringList{QStringLiteral("Ingredient"), QStringLiteral("Level"), QStringLiteral("Stock"),
                    QStringLiteral("Unit"), QStringLiteral("Reorder at"), QStringLiteral("Expiry"),
                    QStringLiteral("Supplier"), QStringLiteral("Status")},
        shelfPanel);
    m_ingredients->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column <= 7; ++column) {
        m_ingredients->horizontalHeader()->setSectionResizeMode(column,
                                                                QHeaderView::ResizeToContents);
    }
    m_ingredients->setMinimumWidth(620);

    auto* shelfEmpty =
        new EmptyState(QStringLiteral("The store room is empty"),
                       QStringLiteral("Add the ingredients your recipes use and stock will be "
                                      "deducted automatically as orders are served."),
                       shelfPanel);
    QPushButton* emptyAddIngredient =
        shelfEmpty->enableAction(QStringLiteral("Add the first ingredient"));

    auto* shelfStack = new QStackedWidget(shelfPanel);
    shelfStack->setObjectName(QStringLiteral("inventoryShelfStack"));
    shelfStack->addWidget(m_ingredients);
    shelfStack->addWidget(shelfEmpty);
    shelfCol->addWidget(shelfStack, 1);
    leftColumn->addWidget(shelfPanel, 3);

    auto* ledgerPanel = new GlassPanel(this);
    auto* ledgerCol = new QVBoxLayout(ledgerPanel);
    ledgerCol->setContentsMargins(kPanelPad, kPanelPad, kPanelPad, kPanelPad);
    ledgerCol->setSpacing(12);

    auto* ledgerHead = new QHBoxLayout;
    ledgerHead->setSpacing(10);
    ledgerHead->addWidget(makeLabel(QStringLiteral("Usage history"),
                                    QStringLiteral("sectionTitle"), ledgerPanel, 13,
                                    QFont::DemiBold));
    auto* ledgerWho = makeLabel(QString(), QStringLiteral("mutedLabel"), ledgerPanel, 11);
    ledgerWho->setObjectName(QStringLiteral("inventoryLedgerWho"));
    ledgerHead->addWidget(ledgerWho);
    ledgerHead->addStretch(1);
    ledgerCol->addLayout(ledgerHead);

    auto* ledger = new ElegantTable(
        QStringList{QStringLiteral("When"), QStringLiteral("Movement"), QStringLiteral("Reason"),
                    QStringLiteral("Note")},
        ledgerPanel);
    ledger->setObjectName(QStringLiteral("inventoryLedger"));
    ledger->setSortingEnabled(false);
    ledger->verticalHeader()->setDefaultSectionSize(36);
    ledger->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ledger->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ledger->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ledger->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    auto* ledgerEmpty = new EmptyState(QStringLiteral("No movements yet"),
                                       QStringLiteral("Deliveries, waste and recipe usage all "
                                                      "appear here."),
                                       ledgerPanel);
    auto* ledgerStack = new QStackedWidget(ledgerPanel);
    ledgerStack->setObjectName(QStringLiteral("inventoryLedgerStack"));
    ledgerStack->addWidget(ledger);
    ledgerStack->addWidget(ledgerEmpty);
    ledgerCol->addWidget(ledgerStack, 1);
    leftColumn->addWidget(ledgerPanel, 2);
    body->addLayout(leftColumn, 3);

    // Right column: alerts, then suppliers ---------------------------------------------------
    auto* rightColumn = new QVBoxLayout;
    rightColumn->setSpacing(kPageSpacing);

    auto* alertPanel = new GlassPanel(this);
    alertPanel->setMinimumWidth(320);
    auto* alertCol = new QVBoxLayout(alertPanel);
    alertCol->setContentsMargins(kPanelPad, kPanelPad, kPanelPad, kPanelPad);
    alertCol->setSpacing(12);
    alertCol->addWidget(makeLabel(QStringLiteral("Alerts"), QStringLiteral("sectionTitle"),
                                  alertPanel, 13, QFont::DemiBold));
    alertCol->addWidget(makeLabel(QStringLiteral("Anything at or below its reorder level, plus "
                                                 "anything expiring inside a week."),
                                  QStringLiteral("mutedLabel"), alertPanel, 11));

    m_alerts = new QListWidget(alertPanel);
    m_alerts->setObjectName(QStringLiteral("inventoryAlerts"));
    m_alerts->setFrameShape(QFrame::NoFrame);
    m_alerts->setSelectionMode(QAbstractItemView::SingleSelection);
    m_alerts->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_alerts->setSpacing(2);
    m_alerts->setCursor(Qt::PointingHandCursor);

    auto* alertEmpty = new EmptyState(QStringLiteral("Nothing needs attention"),
                                      QStringLiteral("Every ingredient is above its reorder "
                                                     "level and none expires this week."),
                                      alertPanel);
    auto* alertStack = new QStackedWidget(alertPanel);
    alertStack->setObjectName(QStringLiteral("inventoryAlertStack"));
    alertStack->addWidget(m_alerts);
    alertStack->addWidget(alertEmpty);
    alertCol->addWidget(alertStack, 1);
    rightColumn->addWidget(alertPanel, 1);

    auto* supplierPanel = new GlassPanel(this);
    auto* supplierCol = new QVBoxLayout(supplierPanel);
    supplierCol->setContentsMargins(kPanelPad, kPanelPad, kPanelPad, kPanelPad);
    supplierCol->setSpacing(12);

    auto* supplierHead = new QHBoxLayout;
    supplierHead->setSpacing(8);
    supplierHead->addWidget(makeLabel(QStringLiteral("Suppliers"),
                                      QStringLiteral("sectionTitle"), supplierPanel, 13,
                                      QFont::DemiBold));
    supplierHead->addStretch(1);
    auto* editSupplierBtn = makeButton(QStringLiteral("Edit"), QStringLiteral("ghostButton"),
                                       supplierPanel);
    auto* addSupplierBtn = makeButton(QStringLiteral("＋"), QStringLiteral("ghostButton"),
                                      supplierPanel);
    addSupplierBtn->setToolTip(QStringLiteral("Add a supplier"));
    supplierHead->addWidget(editSupplierBtn);
    supplierHead->addWidget(addSupplierBtn);
    supplierCol->addLayout(supplierHead);

    m_suppliers = new ElegantTable(
        QStringList{QStringLiteral("Name"), QStringLiteral("Phone"), QStringLiteral("E-mail"),
                    QStringLiteral("Address")},
        supplierPanel);
    m_suppliers->verticalHeader()->setDefaultSectionSize(38);
    m_suppliers->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_suppliers->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_suppliers->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_suppliers->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    auto* supplierEmpty = new EmptyState(QStringLiteral("No suppliers on file"),
                                         QStringLiteral("Record who you buy from so a low-stock "
                                                        "alert says who to ring."),
                                         supplierPanel);
    QPushButton* emptyAddSupplier =
        supplierEmpty->enableAction(QStringLiteral("Add the first supplier"));

    auto* supplierStack = new QStackedWidget(supplierPanel);
    supplierStack->setObjectName(QStringLiteral("inventorySupplierStack"));
    supplierStack->addWidget(m_suppliers);
    supplierStack->addWidget(supplierEmpty);
    supplierCol->addWidget(supplierStack, 1);
    rightColumn->addWidget(supplierPanel, 1);

    body->addLayout(rightColumn, 2);
    page->addLayout(body, 1);

    // --- wiring -----------------------------------------------------------------------------
    connect(addIngredientBtn, &QPushButton::clicked, this, &InventoryPage::onAddIngredient);
    connect(emptyAddIngredient, &QPushButton::clicked, this, &InventoryPage::onAddIngredient);
    connect(editIngredientBtn, &QPushButton::clicked, this, &InventoryPage::onEditIngredient);
    connect(m_ingredients, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*) { onEditIngredient(); });
    connect(restockBtn, &QPushButton::clicked, this, &InventoryPage::onRestock);
    connect(wasteBtn, &QPushButton::clicked, this, &InventoryPage::onWaste);
    connect(addSupplierBtn, &QPushButton::clicked, this, &InventoryPage::onAddSupplier);
    connect(emptyAddSupplier, &QPushButton::clicked, this, &InventoryPage::onAddSupplier);
    connect(editSupplierBtn, &QPushButton::clicked, this, &InventoryPage::onEditSupplier);
    connect(m_suppliers, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*) { onEditSupplier(); });
    connect(search, &QLineEdit::textChanged, this, [this](const QString&) {
        if (BusyGuard::busy(this)) return;
        refresh();
    });
    connect(m_ingredients, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (BusyGuard::busy(this)) return;
        refresh();
    });
    // Clicking an alert jumps to the offending line rather than making the user hunt for it.
    connect(m_alerts, &QListWidget::itemClicked, this, [this](QListWidgetItem* alert) {
        if (!alert) return;
        const int id = alert->data(Qt::UserRole).toInt();
        if (id == 0) return;
        for (int row = 0; row < m_ingredients->rowCount(); ++row) {
            const QTableWidgetItem* cell = m_ingredients->item(row, 0);
            if (cell && cell->data(Qt::UserRole).toInt() == id) {
                m_ingredients->selectRow(row);
                m_ingredients->scrollToItem(m_ingredients->item(row, 0));
                break;
            }
        }
    });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &InventoryPage::refresh);

    new EntranceAnimator(this);
}

// ============================================================================================
//  Page interface
// ============================================================================================

QString InventoryPage::pageTitle() const { return QStringLiteral("Inventory"); }

void InventoryPage::refresh() {
    // Must be idempotent and must never throw — Page's contract.
    if (BusyGuard::busy(this)) return;
    BusyGuard guard(this);
    try {
        const Palette& p = theme();

        const int previousIngredient = selectedIngredientId();
        const int previousSupplier = selectedSupplierId();

        // Suppliers first: the shelf shows supplier names, not ids.
        const std::vector<models::Supplier> suppliers = m_ctx.inventory().suppliers();
        QHash<int, QString> supplierNames;
        for (const models::Supplier& supplier : suppliers) {
            supplierNames.insert(supplier.id(), supplier.name());
        }

        QString filter;
        for (QLineEdit* field : findChildren<QLineEdit*>()) {
            if (field->property("aluchopRole").toString() == QLatin1String("inventorySearch")) {
                filter = field->text().trimmed();
            }
        }

        // --- the shelf ------------------------------------------------------------------------
        const std::vector<models::Ingredient> everything = m_ctx.inventory().all();
        beginRepopulate(m_ingredients);
        int rowIndex = 0;
        int lowCount = 0;
        int expiringCount = 0;
        for (const models::Ingredient& item : everything) {
            const QString supplierName = supplierNames.value(item.supplierId());
            if (!filter.isEmpty()) {
                const bool hit = item.name().contains(filter, Qt::CaseInsensitive) ||
                                 item.unit().contains(filter, Qt::CaseInsensitive) ||
                                 supplierName.contains(filter, Qt::CaseInsensitive);
                if (!hit) continue;
            }

            const StockState state = stateOf(item);
            const QColor stateColour = colourFor(state);
            if (state == StockState::Low || state == StockState::Expired) ++lowCount;
            if (state == StockState::ExpiringSoon || state == StockState::Expired) ++expiringCount;

            m_ingredients->insertRow(rowIndex);

            auto* nameCell = textCell(item.name());
            nameCell->setData(Qt::UserRole, item.id());
            QFont nameFont = nameCell->font();
            nameFont.setWeight(QFont::DemiBold);
            nameCell->setFont(nameFont);
            m_ingredients->setItem(rowIndex, 0, nameCell);

            // Visual gauge: stock against twice its reorder level, coloured by urgency.
            auto* gaugeCell = new SortableCell(levelGauge(item.stockQty(), item.lowThreshold()),
                                               levelRatio(item.stockQty(), item.lowThreshold()));
            gaugeCell->setTextAlignment(Qt::AlignCenter);
            gaugeCell->setForeground(stateColour);
            gaugeCell->setToolTip(QStringLiteral("%1 %2 in store, reorder at %3 %2")
                                      .arg(formatQty(item.stockQty()), item.unit(),
                                           formatQty(item.lowThreshold())));
            m_ingredients->setItem(rowIndex, 1, gaugeCell);

            auto* stockCell = numberCell(formatQty(item.stockQty()), item.stockQty());
            stockCell->setForeground(stateColour);
            QFont stockFont = stockCell->font();
            stockFont.setWeight(QFont::DemiBold);
            stockCell->setFont(stockFont);
            m_ingredients->setItem(rowIndex, 2, stockCell);

            m_ingredients->setItem(rowIndex, 3, textCell(item.unit()));
            m_ingredients->setItem(rowIndex, 4,
                                   numberCell(formatQty(item.lowThreshold()),
                                              item.lowThreshold()));

            const QDate expiry = item.expiryDate();
            QString expiryText = QStringLiteral("—");
            double expiryKey = 1.0e12;  // no expiry sorts last
            if (expiry.isValid()) {
                const qint64 days = QDate::currentDate().daysTo(expiry);
                expiryKey = static_cast<double>(days);
                expiryText = expiry.toString(QStringLiteral("dd MMM yyyy"));
                if (days < 0) {
                    expiryText += QStringLiteral("  (%1d ago)").arg(-days);
                } else if (days <= kExpiryHorizonDays) {
                    expiryText += days == 0 ? QStringLiteral("  (today)")
                                            : QStringLiteral("  (in %1d)").arg(days);
                }
            }
            auto* expiryCell = new SortableCell(expiryText, expiryKey);
            expiryCell->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            if (expiry.isValid() && QDate::currentDate().daysTo(expiry) <= kExpiryHorizonDays) {
                expiryCell->setForeground(expiry < QDate::currentDate() ? p.danger : p.secondary);
            }
            m_ingredients->setItem(rowIndex, 5, expiryCell);

            auto* supplierCell = textCell(supplierName.isEmpty() ? QStringLiteral("—")
                                                                 : supplierName);
            if (supplierName.isEmpty()) supplierCell->setForeground(p.textMuted);
            m_ingredients->setItem(rowIndex, 6, supplierCell);

            auto* statusCell = textCell(labelFor(state));
            statusCell->setForeground(stateColour);
            QFont statusFont = statusCell->font();
            statusFont.setWeight(QFont::DemiBold);
            statusCell->setFont(statusFont);
            m_ingredients->setItem(rowIndex, 7, statusCell);

            // A whole-row wash makes an urgent line readable at a glance across eight columns.
            if (state != StockState::Healthy) {
                QColor wash = stateColour;
                wash.setAlpha(state == StockState::ExpiringSoon ? 22 : 30);
                for (int column = 0; column < m_ingredients->columnCount(); ++column) {
                    if (QTableWidgetItem* cell = m_ingredients->item(rowIndex, column)) {
                        cell->setBackground(wash);
                    }
                }
            }
            ++rowIndex;
        }
        endRepopulate(m_ingredients);

        if (auto* stack = findChild<QStackedWidget*>(QStringLiteral("inventoryShelfStack"))) {
            stack->setCurrentIndex(rowIndex == 0 ? 1 : 0);
            if (auto* empty = dynamic_cast<EmptyState*>(stack->widget(1))) {
                if (rowIndex == 0 && !filter.isEmpty()) {
                    empty->setText(QStringLiteral("No ingredient matches “%1”").arg(filter),
                                   QStringLiteral("Clear the filter to see the whole shelf."));
                } else if (rowIndex == 0) {
                    empty->setText(QStringLiteral("The store room is empty"),
                                   QStringLiteral("Add the ingredients your recipes use and "
                                                  "stock will be deducted automatically as "
                                                  "orders are served."));
                }
            }
        }

        if (previousIngredient != 0) {
            for (int row = 0; row < m_ingredients->rowCount(); ++row) {
                const QTableWidgetItem* cell = m_ingredients->item(row, 0);
                if (cell && cell->data(Qt::UserRole).toInt() == previousIngredient) {
                    m_ingredients->selectRow(row);
                    break;
                }
            }
        }

        if (auto* chip = findChild<QLabel*>(QStringLiteral("inventoryLowChip"))) {
            chip->setText(QStringLiteral("%1 low").arg(lowCount));
            styleChip(chip, lowCount > 0 ? p.danger : p.success);
        }
        if (auto* chip = findChild<QLabel*>(QStringLiteral("inventoryExpiryChip"))) {
            chip->setText(QStringLiteral("%1 expiring").arg(expiringCount));
            styleChip(chip, expiringCount > 0 ? p.secondary : p.success);
        }

        // --- alerts ---------------------------------------------------------------------------
        // The service decides what is low and what is expiring; the list only renders the answer.
        const std::vector<models::Ingredient> low = m_ctx.inventory().lowStock();
        const std::vector<models::Ingredient> expiring =
            m_ctx.inventory().expiring(kExpiryHorizonDays);

        m_alerts->clear();
        for (const models::Ingredient& item : low) {
            auto* entry = new QListWidgetItem(
                QStringLiteral("●  %1 — %2 %3 left, reorder at %4")
                    .arg(item.name(), formatQty(item.stockQty()), item.unit(),
                         formatQty(item.lowThreshold())));
            entry->setForeground(p.danger);
            entry->setData(Qt::UserRole, item.id());
            entry->setToolTip(QStringLiteral("Click to select this ingredient, then Restock."));
            entry->setSizeHint(QSize(0, 34));
            m_alerts->addItem(entry);
        }
        for (const models::Ingredient& item : expiring) {
            const QDate expiry = item.expiryDate();
            const qint64 days = expiry.isValid() ? QDate::currentDate().daysTo(expiry) : 0;
            const QString when = days < 0 ? QStringLiteral("expired %1 day(s) ago").arg(-days)
                                 : days == 0 ? QStringLiteral("expires today")
                                             : QStringLiteral("expires in %1 day(s)").arg(days);
            auto* entry = new QListWidgetItem(
                QStringLiteral("●  %1 — %2").arg(item.name(), when));
            entry->setForeground(days < 0 ? p.danger : p.secondary);
            entry->setData(Qt::UserRole, item.id());
            entry->setSizeHint(QSize(0, 34));
            m_alerts->addItem(entry);
        }
        if (auto* stack = findChild<QStackedWidget*>(QStringLiteral("inventoryAlertStack"))) {
            stack->setCurrentIndex(m_alerts->count() == 0 ? 1 : 0);
        }

        // --- suppliers ------------------------------------------------------------------------
        beginRepopulate(m_suppliers);
        int supplierRow = 0;
        for (const models::Supplier& supplier : suppliers) {
            m_suppliers->insertRow(supplierRow);
            auto* nameCell = textCell(supplier.name());
            nameCell->setData(Qt::UserRole, supplier.id());
            QFont nameFont = nameCell->font();
            nameFont.setWeight(QFont::DemiBold);
            nameCell->setFont(nameFont);
            m_suppliers->setItem(supplierRow, 0, nameCell);
            m_suppliers->setItem(supplierRow, 1,
                                 textCell(supplier.phone().isEmpty() ? QStringLiteral("—")
                                                                     : supplier.phone()));
            m_suppliers->setItem(supplierRow, 2,
                                 textCell(supplier.email().isEmpty() ? QStringLiteral("—")
                                                                     : supplier.email()));
            m_suppliers->setItem(supplierRow, 3,
                                 textCell(supplier.address().isEmpty() ? QStringLiteral("—")
                                                                       : supplier.address()));
            ++supplierRow;
        }
        endRepopulate(m_suppliers);

        if (auto* stack = findChild<QStackedWidget*>(QStringLiteral("inventorySupplierStack"))) {
            stack->setCurrentIndex(supplierRow == 0 ? 1 : 0);
        }
        if (previousSupplier != 0) {
            for (int row = 0; row < m_suppliers->rowCount(); ++row) {
                const QTableWidgetItem* cell = m_suppliers->item(row, 0);
                if (cell && cell->data(Qt::UserRole).toInt() == previousSupplier) {
                    m_suppliers->selectRow(row);
                    break;
                }
            }
        }

        // --- ledger of the selected ingredient --------------------------------------------------
        auto* ledger = findChild<QTableWidget*>(QStringLiteral("inventoryLedger"));
        auto* ledgerStack = findChild<QStackedWidget*>(QStringLiteral("inventoryLedgerStack"));
        auto* ledgerWho = findChild<QLabel*>(QStringLiteral("inventoryLedgerWho"));
        const int ingredientId = selectedIngredientId();

        if (ledger) {
            beginRepopulate(ledger);
            if (ingredientId == 0) {
                if (ledgerWho) ledgerWho->setText(QStringLiteral("— select an ingredient"));
                if (ledgerStack) {
                    ledgerStack->setCurrentIndex(1);
                    if (auto* empty = dynamic_cast<EmptyState*>(ledgerStack->widget(1))) {
                        empty->setText(QStringLiteral("No ingredient selected"),
                                       QStringLiteral("Pick a line on the shelf to see every "
                                                      "delivery, write-off and recipe "
                                                      "deduction against it."));
                    }
                }
            } else {
                QString ingredientName;
                QString ingredientUnit;
                for (const models::Ingredient& item : everything) {
                    if (item.id() == ingredientId) {
                        ingredientName = item.name();
                        ingredientUnit = item.unit();
                        break;
                    }
                }
                if (ledgerWho) ledgerWho->setText(QStringLiteral("· %1").arg(ingredientName));

                const auto movements = m_ctx.inventory().history(ingredientId);
                int ledgerRow = 0;
                for (const auto& movement : movements) {
                    const QDateTime when = std::get<0>(movement);
                    const double delta = std::get<1>(movement);
                    const QString reason = std::get<2>(movement);
                    const QString note = std::get<3>(movement);

                    ledger->insertRow(ledgerRow);
                    ledger->setItem(ledgerRow, 0,
                                    textCell(when.toLocalTime().toString(
                                        QStringLiteral("dd MMM · HH:mm"))));

                    const QString sign = delta >= 0 ? QStringLiteral("+") : QStringLiteral("−");
                    auto* deltaCell =
                        numberCell(QStringLiteral("%1%2 %3")
                                       .arg(sign, formatQty(delta < 0 ? -delta : delta),
                                            ingredientUnit),
                                   delta);
                    deltaCell->setForeground(delta >= 0 ? p.success : p.danger);
                    ledger->setItem(ledgerRow, 1, deltaCell);

                    auto* reasonCell = textCell(reason);
                    reasonCell->setForeground(p.textMuted);
                    ledger->setItem(ledgerRow, 2, reasonCell);
                    ledger->setItem(ledgerRow, 3,
                                    textCell(note.isEmpty() ? QStringLiteral("—") : note));
                    ++ledgerRow;
                }
                if (ledgerStack) {
                    ledgerStack->setCurrentIndex(ledgerRow == 0 ? 1 : 0);
                    if (auto* empty = dynamic_cast<EmptyState*>(ledgerStack->widget(1))) {
                        empty->setText(QStringLiteral("No movements for %1").arg(ingredientName),
                                       QStringLiteral("Restock it, or serve an order whose "
                                                      "recipe uses it."));
                    }
                }
            }
        }

        // --- header subtitle --------------------------------------------------------------------
        for (QLabel* label : findChildren<QLabel*>()) {
            if (label->property("aluchopRole").toString() == QLatin1String("inventorySubtitle")) {
                label->setText(QStringLiteral("%1 ingredient%2  ·  %3 supplier%4  ·  %5 need "
                                              "attention")
                                   .arg(everything.size())
                                   .arg(everything.size() == 1 ? QString() : QStringLiteral("s"))
                                   .arg(suppliers.size())
                                   .arg(suppliers.size() == 1 ? QString() : QStringLiteral("s"))
                                   .arg(m_alerts->count()));
            }
        }
    } catch (const std::exception& e) {
        m_ctx.notifications().notify(QStringLiteral("Inventory"), QString::fromUtf8(e.what()), 3);
    }
}

// ============================================================================================
//  Slots — ingredients
// ============================================================================================

void InventoryPage::onAddIngredient() {
    const std::vector<models::Supplier> suppliers = m_ctx.inventory().suppliers();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("New ingredient"));
    dialog.setMinimumWidth(460);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(22, 22, 22, 22);
    column->setSpacing(16);
    column->addWidget(makeLabel(QStringLiteral("Add a stock item"), QStringLiteral("sectionTitle"),
                                &dialog, 16, QFont::DemiBold));
    column->addWidget(makeLabel(QStringLiteral("Quantities are physical measurements, so they are "
                                               "decimals; the unit cost is money and is kept in "
                                               "exact paisa."),
                                QStringLiteral("mutedLabel"), &dialog, 11));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    auto* name = new QLineEdit(&dialog);
    name->setMinimumHeight(kControlH);
    name->setPlaceholderText(QStringLiteral("e.g. Chicken breast"));
    auto* unit = new QComboBox(&dialog);
    unit->setEditable(true);
    unit->addItems(QStringList{QStringLiteral("kg"), QStringLiteral("g"), QStringLiteral("l"),
                               QStringLiteral("ml"), QStringLiteral("pcs"),
                               QStringLiteral("packet")});
    unit->setMinimumHeight(kControlH);
    auto* stock = new QDoubleSpinBox(&dialog);
    stock->setRange(0.0, 1000000.0);
    stock->setDecimals(2);
    stock->setSingleStep(0.5);
    auto* threshold = new QDoubleSpinBox(&dialog);
    threshold->setRange(0.0, 1000000.0);
    threshold->setDecimals(2);
    threshold->setSingleStep(0.5);
    threshold->setValue(5.0);
    MoneyField cost = makeMoneyField(&dialog, Money::zero());
    auto* supplier = new QComboBox(&dialog);
    supplier->addItem(QStringLiteral("— none —"), 0);
    for (const models::Supplier& s : suppliers) supplier->addItem(s.name(), s.id());
    supplier->setMinimumHeight(kControlH);
    auto* hasExpiry = new QCheckBox(QStringLiteral("This item expires"), &dialog);
    hasExpiry->setCursor(Qt::PointingHandCursor);
    auto* expiry = new QDateEdit(QDate::currentDate().addDays(30), &dialog);
    expiry->setCalendarPopup(true);
    expiry->setDisplayFormat(QStringLiteral("dd MMM yyyy"));
    expiry->setMinimumHeight(kControlH);
    expiry->setEnabled(false);
    connect(hasExpiry, &QCheckBox::toggled, expiry, &QDateEdit::setEnabled);

    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Unit"), unit);
    form->addRow(QStringLiteral("Opening stock"), stock);
    form->addRow(QStringLiteral("Reorder level"), threshold);
    form->addRow(QStringLiteral("Cost per unit"), cost.widget);
    form->addRow(QStringLiteral("Supplier"), supplier);
    form->addRow(QString(), hasExpiry);
    form->addRow(QStringLiteral("Expires on"), expiry);
    column->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Add"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primaryButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    models::Ingredient item;
    try {
        // The model's own setters are the validation gate — a blank name or a negative quantity
        // never reaches SQLite.
        item = models::Ingredient(0, name->text().trimmed(), unit->currentText().trimmed(),
                                  stock->value(), threshold->value());
        item.setUnitCost(cost.value());
        item.setSupplierId(supplier->currentData().toInt());
        if (hasExpiry->isChecked()) item.setExpiryDate(expiry->date());
    } catch (const core::ValidationException& e) {
        m_ctx.notifications().notify(QStringLiteral("Invalid ingredient"),
                                     QString::fromUtf8(e.what()), 3);
        return;
    }

    const auto added = m_ctx.inventory().addIngredient(item);
    if (added.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Ingredient added"),
                                     QStringLiteral("%1 is now on the shelf.").arg(item.name()),
                                     1);
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not add"), added.error(), 3);
    }
}

void InventoryPage::onEditIngredient() {
    const int id = selectedIngredientId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Nothing selected"),
                                     QStringLiteral("Choose an ingredient on the shelf first."),
                                     2);
        return;
    }

    const std::vector<models::Ingredient> everything = m_ctx.inventory().all();
    const auto found = std::find_if(everything.begin(), everything.end(),
                                    [id](const models::Ingredient& i) { return i.id() == id; });
    if (found == everything.end()) {
        m_ctx.notifications().notify(QStringLiteral("Ingredient missing"),
                                     QStringLiteral("That stock item is no longer present."), 3);
        refresh();
        return;
    }
    const models::Ingredient existing = *found;
    const std::vector<models::Supplier> suppliers = m_ctx.inventory().suppliers();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit ingredient"));
    dialog.setMinimumWidth(460);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(22, 22, 22, 22);
    column->setSpacing(16);
    column->addWidget(makeLabel(QStringLiteral("Edit %1").arg(existing.name()),
                                QStringLiteral("sectionTitle"), &dialog, 16, QFont::DemiBold));
    column->addWidget(makeLabel(QStringLiteral("Stock on hand is changed by restocking or by "
                                               "recording waste, never by typing over it — that "
                                               "is what keeps the ledger honest."),
                                QStringLiteral("mutedLabel"), &dialog, 11));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    auto* name = new QLineEdit(existing.name(), &dialog);
    name->setMinimumHeight(kControlH);
    auto* unit = new QComboBox(&dialog);
    unit->setEditable(true);
    unit->addItems(QStringList{QStringLiteral("kg"), QStringLiteral("g"), QStringLiteral("l"),
                               QStringLiteral("ml"), QStringLiteral("pcs"),
                               QStringLiteral("packet")});
    unit->setCurrentText(existing.unit());
    unit->setMinimumHeight(kControlH);
    auto* threshold = new QDoubleSpinBox(&dialog);
    threshold->setRange(0.0, 1000000.0);
    threshold->setDecimals(2);
    threshold->setSingleStep(0.5);
    threshold->setValue(existing.lowThreshold());
    MoneyField cost = makeMoneyField(&dialog, existing.unitCost());
    auto* supplier = new QComboBox(&dialog);
    supplier->addItem(QStringLiteral("— none —"), 0);
    for (const models::Supplier& s : suppliers) supplier->addItem(s.name(), s.id());
    const int supplierIndex = supplier->findData(existing.supplierId());
    supplier->setCurrentIndex(supplierIndex >= 0 ? supplierIndex : 0);
    supplier->setMinimumHeight(kControlH);
    auto* hasExpiry = new QCheckBox(QStringLiteral("This item expires"), &dialog);
    hasExpiry->setCursor(Qt::PointingHandCursor);
    hasExpiry->setChecked(existing.expiryDate().isValid());
    auto* expiry = new QDateEdit(existing.expiryDate().isValid()
                                     ? existing.expiryDate()
                                     : QDate::currentDate().addDays(30),
                                 &dialog);
    expiry->setCalendarPopup(true);
    expiry->setDisplayFormat(QStringLiteral("dd MMM yyyy"));
    expiry->setMinimumHeight(kControlH);
    expiry->setEnabled(hasExpiry->isChecked());
    connect(hasExpiry, &QCheckBox::toggled, expiry, &QDateEdit::setEnabled);

    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Unit"), unit);
    form->addRow(QStringLiteral("In store"),
                 makeLabel(QStringLiteral("%1 %2").arg(formatQty(existing.stockQty()),
                                                       existing.unit()),
                           QString(), &dialog, 12, QFont::DemiBold));
    form->addRow(QStringLiteral("Reorder level"), threshold);
    form->addRow(QStringLiteral("Cost per unit"), cost.widget);
    form->addRow(QStringLiteral("Supplier"), supplier);
    form->addRow(QString(), hasExpiry);
    form->addRow(QStringLiteral("Expires on"), expiry);
    column->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primaryButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    models::Ingredient edited = existing;
    try {
        edited.setName(name->text().trimmed());
        edited.setUnit(unit->currentText().trimmed());
        edited.setLowThreshold(threshold->value());
        edited.setUnitCost(cost.value());
        edited.setSupplierId(supplier->currentData().toInt());
        edited.setExpiryDate(hasExpiry->isChecked() ? expiry->date() : QDate());
    } catch (const core::ValidationException& e) {
        m_ctx.notifications().notify(QStringLiteral("Invalid ingredient"),
                                     QString::fromUtf8(e.what()), 3);
        return;
    }

    const auto saved = m_ctx.inventory().updateIngredient(edited);
    if (saved.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Ingredient saved"),
                                     QStringLiteral("%1 was updated.").arg(edited.name()), 1);
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not save"), saved.error(), 3);
    }
}

void InventoryPage::onRestock() {
    const int id = selectedIngredientId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Nothing selected"),
                                     QStringLiteral("Choose the ingredient you are booking in."),
                                     2);
        return;
    }
    const std::vector<models::Ingredient> everything = m_ctx.inventory().all();
    const auto found = std::find_if(everything.begin(), everything.end(),
                                    [id](const models::Ingredient& i) { return i.id() == id; });
    if (found == everything.end()) {
        refresh();
        return;
    }
    const models::Ingredient existing = *found;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Restock"));
    dialog.setMinimumWidth(440);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(22, 22, 22, 22);
    column->setSpacing(16);
    column->addWidget(makeLabel(QStringLiteral("Book in a delivery"),
                                QStringLiteral("sectionTitle"), &dialog, 16, QFont::DemiBold));
    column->addWidget(makeLabel(QStringLiteral("%1 — %2 %3 in store, reorder level %4 %3")
                                    .arg(existing.name(), formatQty(existing.stockQty()),
                                         existing.unit(), formatQty(existing.lowThreshold())),
                                QStringLiteral("mutedLabel"), &dialog, 11));
    column->addWidget(makeLabel(QStringLiteral("This movement is undoable — Ctrl+Z books the "
                                               "same quantity straight back out."),
                                QStringLiteral("mutedLabel"), &dialog, 11));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    auto* qty = new QDoubleSpinBox(&dialog);
    qty->setRange(0.01, 1000000.0);
    qty->setDecimals(2);
    qty->setSingleStep(1.0);
    qty->setValue(std::max(1.0, existing.lowThreshold()));
    qty->setSuffix(QStringLiteral(" ") + existing.unit());
    MoneyField cost = makeMoneyField(&dialog, existing.unitCost());
    auto* note = new QLineEdit(&dialog);
    note->setMinimumHeight(kControlH);
    note->setPlaceholderText(QStringLiteral("Delivery note or invoice number (optional)"));

    form->addRow(QStringLiteral("Quantity"), qty);
    form->addRow(QStringLiteral("Cost per unit"), cost.widget);
    form->addRow(QStringLiteral("Note"), note);
    column->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Book in"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primaryButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    // Undoable: the edit is expressed as a Command and handed to the application-wide stack,
    // which converts a thrown core::InventoryException into a Result the toast can show.
    auto command = std::make_unique<services::AdjustStockCommand>(
        m_ctx.inventory(), id, qty->value(), cost.value(), note->text().trimmed());
    const auto ran = m_ctx.commands().run(std::move(command));
    if (ran.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Stock booked in"),
                                     QStringLiteral("+%1 %2 of %3 — Ctrl+Z to undo.")
                                         .arg(formatQty(qty->value()), existing.unit(),
                                              existing.name()),
                                     1);
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not restock"), ran.error(), 3);
    }
}

void InventoryPage::onWaste() {
    const int id = selectedIngredientId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Nothing selected"),
                                     QStringLiteral("Choose the ingredient you are writing off."),
                                     2);
        return;
    }
    const std::vector<models::Ingredient> everything = m_ctx.inventory().all();
    const auto found = std::find_if(everything.begin(), everything.end(),
                                    [id](const models::Ingredient& i) { return i.id() == id; });
    if (found == everything.end()) {
        refresh();
        return;
    }
    const models::Ingredient existing = *found;

    if (existing.stockQty() <= 0.0) {
        m_ctx.notifications().notify(QStringLiteral("Nothing to write off"),
                                     QStringLiteral("%1 already shows zero in store.")
                                         .arg(existing.name()),
                                     2);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Record waste"));
    dialog.setMinimumWidth(440);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(22, 22, 22, 22);
    column->setSpacing(16);
    column->addWidget(makeLabel(QStringLiteral("Write off spoiled stock"),
                                QStringLiteral("sectionTitle"), &dialog, 16, QFont::DemiBold));
    column->addWidget(makeLabel(QStringLiteral("%1 — %2 %3 in store. The reason is mandatory: a "
                                               "write-off without one is indistinguishable from "
                                               "theft.")
                                    .arg(existing.name(), formatQty(existing.stockQty()),
                                         existing.unit()),
                                QStringLiteral("mutedLabel"), &dialog, 11));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    auto* qty = new QDoubleSpinBox(&dialog);
    qty->setRange(0.01, std::max(0.01, existing.stockQty()));
    qty->setDecimals(2);
    qty->setSingleStep(0.5);
    qty->setValue(std::min(1.0, std::max(0.01, existing.stockQty())));
    qty->setSuffix(QStringLiteral(" ") + existing.unit());
    auto* reason = new QComboBox(&dialog);
    reason->setEditable(true);
    reason->addItems(QStringList{QStringLiteral("Spoiled"), QStringLiteral("Expired"),
                                 QStringLiteral("Spilled"), QStringLiteral("Damaged in "
                                                                          "delivery"),
                                 QStringLiteral("Stock count correction")});
    reason->setMinimumHeight(kControlH);

    form->addRow(QStringLiteral("Quantity"), qty);
    form->addRow(QStringLiteral("Reason"), reason);
    column->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Write off"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("dangerButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    if (reason->currentText().trimmed().isEmpty()) {
        m_ctx.notifications().notify(QStringLiteral("Reason required"),
                                     QStringLiteral("Say why the stock is being written off."),
                                     2);
        return;
    }

    const auto written = m_ctx.inventory().recordWaste(id, qty->value(),
                                                       reason->currentText().trimmed());
    if (written.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Waste recorded"),
                                     QStringLiteral("−%1 %2 of %3 written off.")
                                         .arg(formatQty(qty->value()), existing.unit(),
                                              existing.name()),
                                     2);
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not record waste"), written.error(), 3);
    }
}

// ============================================================================================
//  Slots — suppliers
// ============================================================================================

void InventoryPage::onAddSupplier() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("New supplier"));
    dialog.setMinimumWidth(440);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(22, 22, 22, 22);
    column->setSpacing(16);
    column->addWidget(makeLabel(QStringLiteral("Add a supplier"), QStringLiteral("sectionTitle"),
                                &dialog, 16, QFont::DemiBold));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    auto* name = new QLineEdit(&dialog);
    name->setMinimumHeight(kControlH);
    name->setPlaceholderText(QStringLiteral("Company name"));
    auto* phone = new QLineEdit(&dialog);
    phone->setMinimumHeight(kControlH);
    auto* email = new QLineEdit(&dialog);
    email->setMinimumHeight(kControlH);
    auto* address = new QLineEdit(&dialog);
    address->setMinimumHeight(kControlH);
    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Phone"), phone);
    form->addRow(QStringLiteral("E-mail"), email);
    form->addRow(QStringLiteral("Address"), address);
    column->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Add"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primaryButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    models::Supplier supplier;
    try {
        supplier = models::Supplier(0, name->text().trimmed(), phone->text().trimmed(),
                                    email->text().trimmed(), address->text().trimmed());
    } catch (const core::ValidationException& e) {
        m_ctx.notifications().notify(QStringLiteral("Invalid supplier"),
                                     QString::fromUtf8(e.what()), 3);
        return;
    }

    const auto added = m_ctx.inventory().addSupplier(supplier);
    if (added.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Supplier added"),
                                     QStringLiteral("%1 is now on file.").arg(supplier.name()), 1);
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not add supplier"), added.error(), 3);
    }
}

void InventoryPage::onEditSupplier() {
    const int id = selectedSupplierId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Nothing selected"),
                                     QStringLiteral("Choose a supplier first."), 2);
        return;
    }
    const std::vector<models::Supplier> suppliers = m_ctx.inventory().suppliers();
    const auto found = std::find_if(suppliers.begin(), suppliers.end(),
                                    [id](const models::Supplier& s) { return s.id() == id; });
    if (found == suppliers.end()) {
        m_ctx.notifications().notify(QStringLiteral("Supplier missing"),
                                     QStringLiteral("That supplier is no longer on file."), 3);
        refresh();
        return;
    }
    const models::Supplier existing = *found;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit supplier"));
    dialog.setMinimumWidth(440);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(22, 22, 22, 22);
    column->setSpacing(16);
    column->addWidget(makeLabel(QStringLiteral("Edit %1").arg(existing.name()),
                                QStringLiteral("sectionTitle"), &dialog, 16, QFont::DemiBold));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    auto* name = new QLineEdit(existing.name(), &dialog);
    name->setMinimumHeight(kControlH);
    auto* phone = new QLineEdit(existing.phone(), &dialog);
    phone->setMinimumHeight(kControlH);
    auto* email = new QLineEdit(existing.email(), &dialog);
    email->setMinimumHeight(kControlH);
    auto* address = new QLineEdit(existing.address(), &dialog);
    address->setMinimumHeight(kControlH);
    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Phone"), phone);
    form->addRow(QStringLiteral("E-mail"), email);
    form->addRow(QStringLiteral("Address"), address);
    column->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primaryButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    models::Supplier edited = existing;
    try {
        edited.setName(name->text().trimmed());
        edited.setPhone(phone->text().trimmed());
        edited.setEmail(email->text().trimmed());
        edited.setAddress(address->text().trimmed());
    } catch (const core::ValidationException& e) {
        m_ctx.notifications().notify(QStringLiteral("Invalid supplier"),
                                     QString::fromUtf8(e.what()), 3);
        return;
    }

    const auto saved = m_ctx.inventory().updateSupplier(edited);
    if (saved.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Supplier saved"),
                                     QStringLiteral("%1 was updated.").arg(edited.name()), 1);
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not save supplier"), saved.error(), 3);
    }
}

// ============================================================================================
//  Helpers
// ============================================================================================

int InventoryPage::selectedIngredientId() const {
    if (!m_ingredients) return 0;
    const int row = m_ingredients->currentRow();
    if (row < 0 || row >= m_ingredients->rowCount()) return 0;
    if (!m_ingredients->selectionModel() || !m_ingredients->selectionModel()->hasSelection()) {
        return 0;
    }
    const QTableWidgetItem* cell = m_ingredients->item(row, 0);
    return cell ? cell->data(Qt::UserRole).toInt() : 0;
}

int InventoryPage::selectedSupplierId() const {
    if (!m_suppliers) return 0;
    const int row = m_suppliers->currentRow();
    if (row < 0 || row >= m_suppliers->rowCount()) return 0;
    if (!m_suppliers->selectionModel() || !m_suppliers->selectionModel()->hasSelection()) return 0;
    const QTableWidgetItem* cell = m_suppliers->item(row, 0);
    return cell ? cell->data(Qt::UserRole).toInt() : 0;
}

}  // namespace aluchop::gui

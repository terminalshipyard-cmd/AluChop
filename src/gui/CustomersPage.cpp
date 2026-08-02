/**
 * @file CustomersPage.cpp
 * @brief Screen 4 — customer database, loyalty points, visit history and favourites.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Presentation only. Every fact on this screen is fetched from
 * services::CustomerService; the screen owns no rules, performs no arithmetic on
 * loyalty points and contains no SQL. The "Visits" column is the value that
 * models::Customer::operator++ maintains, and the "Loyalty" column is the balance
 * BillingService awards when a bill is settled.
 */

#include "aluchop/gui/CustomersPage.hpp"

#include <algorithm>
#include <initializer_list>
#include <vector>

#include <QAbstractAnimation>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEasingCurve>
#include <QEvent>
#include <QFont>
#include <QFormLayout>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QPushButton>
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

#include "aluchop/core/Money.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/gui/Widgets.hpp"
#include "aluchop/models/Customer.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/models/OrderItem.hpp"
#include "aluchop/services/AppContext.hpp"

namespace aluchop::gui {
namespace {

using core::Money;

// --- house measurements, shared by all four management screens -----------------------------
constexpr int kPageMargin = 24;    ///< outer gutter of every management screen
constexpr int kPageSpacing = 18;   ///< vertical rhythm between page bands
constexpr int kPanelPad = 18;      ///< inner padding of a card / glass panel
constexpr int kControlH = 38;      ///< uniform height of buttons and inputs
constexpr int kFadeMs = 220;       ///< entrance animation duration
constexpr int kRowH = 44;          ///< ElegantTable's row height — panels size in whole rows

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

/// @brief Builds a themed button. @p variant is one of the QSS object names
///        `primaryButton` / `ghostButton` / `dangerButton` (ThemeManager QSS contract).
QPushButton* makeButton(const QString& text, const QString& variant, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(variant);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(kControlH);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}

/// @brief A card surface with the house shadow.
GlassPanel* makePanel(QWidget* parent) {
    auto* panel = new GlassPanel(parent);
    return panel;
}

/// @brief Rounded "pill" used for the loyalty and visit counters.
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

/// @brief Repaints a chip from the live palette (called from refresh(), so Light/Dark switch live).
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
///              "Rs 1,250.00" sorts above "Rs 900.00" instead of sorting as text
class SortableCell : public QTableWidgetItem {
public:
    /// @param text display text (already formatted for humans).
    /// @param key hidden ordering key.
    SortableCell(const QString& text, double key) : QTableWidgetItem(text), m_key(key) {}

    /// @return true when this cell orders before @p other.
    bool operator<(const QTableWidgetItem& other) const override {
        const auto* rhs = dynamic_cast<const SortableCell*>(&other);
        return rhs ? m_key < rhs->m_key : QTableWidgetItem::operator<(other);
    }

private:
    double m_key;  ///< numeric ordering key behind the formatted text
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
    f.setFeature(QFont::Tag("tnum"), 1);  // tabular figures: digits line up column-wise
#endif
    cell->setFont(f);
    return cell;
}

/// @brief Plays a one-off fade the first time its target screen is shown.
///
/// Page has no virtual showEvent to override (the header contract is frozen), so the entrance
/// animation is attached as an event filter instead — which is the idiomatic Qt way to react to
/// another object's events without subclassing it.
class EntranceAnimator : public QObject {
public:
    /// @param target widget to fade in; also becomes the Qt parent, so this object dies with it.
    explicit EntranceAnimator(QWidget* target) : QObject(target), m_target(target) {
        target->installEventFilter(this);
    }

protected:
    /// @return false so the event continues to its normal destination.
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
                // Drop the effect once it has served its purpose: a permanent graphics
                // effect forces every repaint through an offscreen surface.
                QTimer::singleShot(0, target, [target]() { target->setGraphicsEffect(nullptr); });
            });
            fade->start(QAbstractAnimation::DeleteWhenStopped);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget* m_target;      ///< screen being animated
    bool m_played = false;  ///< the entrance plays exactly once
};

/// @brief Gives a table a column policy that cannot starve its identity column.
///
/// `ElegantTable` turns on `stretchLastSection`, so the last column hoards the spare width while
/// an earlier `Stretch` column is squeezed to the header minimum — which is how a directory with
/// room to spare still showed "sujan.adhikari@exa…".
///
/// The rule this settles on: **exactly one Stretch column, and it is the identity column.**
/// Everything else — a phone, an e-mail, a count, a points balance — is `ResizeToContents`, so it
/// takes precisely the width its longest value needs and never a pixel less. An e-mail address
/// therefore cannot be elided by a name column sitting on slack it does not need, and the name
/// column still absorbs whatever is left over.
///
/// @param table          the table to configure.
/// @param stretchColumns the identity column(s) that absorb the spare width.
/// @param minSection     narrowest any column may become, in pixels.
void configureColumns(QTableWidget* table, std::initializer_list<int> stretchColumns,
                      int minSection = 76) {
    QHeaderView* head = table->horizontalHeader();
    head->setStretchLastSection(false);
    head->setMinimumSectionSize(minSection);
    for (int column = 0; column < table->columnCount(); ++column) {
        const bool stretch =
            std::find(stretchColumns.begin(), stretchColumns.end(), column) !=
            stretchColumns.end();
        head->setSectionResizeMode(column, stretch ? QHeaderView::Stretch
                                                   : QHeaderView::ResizeToContents);
    }
    table->setTextElideMode(Qt::ElideRight);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
}

/// @brief Opens a sortable table A→Z on @p column instead of Z→A.
///
/// `QHeaderView` starts with a descending sort indicator, and `setSortingEnabled(true)` honours
/// it — which is why a customer directory opened on "Sujan … Aakriti". A directory is read
/// alphabetically, so the indicator is set explicitly.
void sortAscendingBy(QTableWidget* table, int column) {
    table->horizontalHeader()->setSortIndicator(column, Qt::AscendingOrder);
    table->sortItems(column, Qt::AscendingOrder);
}

/// @brief Repeats a cell's own text as its tooltip, so an elided cell is never lost data.
QTableWidgetItem* withTooltip(QTableWidgetItem* cell, const QString& detail = QString()) {
    cell->setToolTip(detail.isEmpty() ? cell->text() : detail);
    return cell;
}

/// @brief Keeps a table an exact number of whole rows tall.
///
/// A panel hands its table whatever height is left over, which is almost never a whole multiple
/// of the row height — so the bottom row was drawn sliced through the middle. Capping the table
/// at `header + n · rowHeight` turns those leftover pixels into bottom padding inside the card,
/// which reads as deliberate space instead of a clipped row. The height is measured from the
/// container the layout actually resizes, never from the (capped) table, so the table still grows
/// when the window does.
///
/// @oop-concept Method Overriding :: QObject::eventFilter is overridden to react to another
///              widget's resize without subclassing that widget
class WholeRowFitter : public QObject {
public:
    /// @param table the table to snap.
    /// @param container the widget the layout resizes (the table's stack); also the Qt parent.
    WholeRowFitter(QTableWidget* table, QWidget* container)
        : QObject(container), m_table(table), m_container(container) {
        container->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == m_container &&
            (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
            snap();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void snap() {
        const int rowH = m_table->verticalHeader()->defaultSectionSize();
        const int headH = m_table->horizontalHeader()->sizeHint().height();
        const int available = m_container->height() - headH;
        if (rowH <= 0 || available < rowH) return;
        const int target = headH + (available / rowH) * rowH;
        if (m_table->maximumHeight() != target) m_table->setMaximumHeight(target);
    }

    QTableWidget* m_table;
    QWidget* m_container;
};

/// @brief Prepares a table for a full repopulation (sorting off while rows are inserted).
void beginRepopulate(QTableWidget* table) {
    table->setSortingEnabled(false);
    table->clearContents();
    table->setRowCount(0);
}

/// @brief Re-enables sorting after a repopulation.
void endRepopulate(QTableWidget* table) { table->setSortingEnabled(true); }

/// @brief Marks a screen as "repopulating" for as long as it is in scope.
///
/// Clearing and refilling a QTableWidget emits itemSelectionChanged several times. Without this
/// flag the selection handler would run against a half-built table in the middle of refresh().
///
/// @oop-concept Destructor / RAII :: the flag is cleared by scope exit, including the path where
///              an exception unwinds out of the middle of a repopulation
class BusyGuard {
public:
    /// @param screen the page being repopulated.
    explicit BusyGuard(QObject* screen) : m_screen(screen) {
        m_screen->setProperty("aluchopBusy", true);
    }
    ~BusyGuard() { m_screen->setProperty("aluchopBusy", false); }

    BusyGuard(const BusyGuard&) = delete;
    BusyGuard& operator=(const BusyGuard&) = delete;

    /// @return true while @p screen is mid-repopulation.
    static bool busy(const QObject* screen) {
        return screen->property("aluchopBusy").toBool();
    }

private:
    QObject* m_screen;
};

}  // namespace

// ============================================================================================
//  Construction
// ============================================================================================

CustomersPage::CustomersPage(services::AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    setObjectName(QStringLiteral("customersPage"));

    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    page->setSpacing(kPageSpacing);

    // --- header band ------------------------------------------------------------------------
    auto* header = new QHBoxLayout;
    header->setSpacing(12);

    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    titles->addWidget(makeLabel(pageTitle(), QStringLiteral("pageTitle"), this, 24, QFont::Bold));
    auto* subtitle =
        makeLabel(QStringLiteral("Loading the loyalty programme…"),
                  QStringLiteral("mutedLabel"), this, 11);
    subtitle->setObjectName(QStringLiteral("mutedLabel"));
    subtitle->setProperty("aluchopRole", QStringLiteral("customersSubtitle"));
    titles->addWidget(subtitle);
    header->addLayout(titles);
    header->addStretch(1);

    auto* addBtn = makeButton(QStringLiteral("＋  New customer"),
                              QStringLiteral("primaryButton"), this);
    auto* editBtn = makeButton(QStringLiteral("Edit"), QStringLiteral("ghostButton"), this);
    auto* deleteBtn = makeButton(QStringLiteral("Remove"), QStringLiteral("dangerButton"), this);
    editBtn->setObjectName(QStringLiteral("ghostButton"));
    header->addWidget(editBtn);
    header->addWidget(deleteBtn);
    header->addWidget(addBtn);
    page->addLayout(header);

    // --- search band ------------------------------------------------------------------------
    auto* searchRow = new QHBoxLayout;
    searchRow->setSpacing(12);
    m_search = new SearchBar(QStringLiteral("Search by name, phone or e-mail…"), this);
    m_search->setMinimumHeight(kControlH);
    searchRow->addWidget(m_search, 1);
    auto* searchBtn = makeButton(QStringLiteral("Search"), QStringLiteral("ghostButton"), this);
    searchRow->addWidget(searchBtn);
    page->addLayout(searchRow);

    // --- body: directory (left) + guest profile (right) ---------------------------------------
    auto* body = new QHBoxLayout;
    body->setSpacing(kPageSpacing);

    // Directory panel ------------------------------------------------------------------------
    auto* directory = makePanel(this);
    auto* directoryCol = new QVBoxLayout(directory);
    directoryCol->setContentsMargins(kPanelPad, kPanelPad, kPanelPad, kPanelPad);
    directoryCol->setSpacing(12);
    directoryCol->addWidget(
        makeLabel(QStringLiteral("Directory"), QStringLiteral("sectionTitle"), directory, 13,
                  QFont::DemiBold));

    m_table = new ElegantTable(QStringList{QStringLiteral("Name"), QStringLiteral("Phone"),
                                           QStringLiteral("E-mail"), QStringLiteral("Visits"),
                                           QStringLiteral("Loyalty")},
                               directory);
    // Only the name stretches. The e-mail column then sizes to the longest address on file, which
    // is what stops "rojina.maharjan@exa…" happening beside a name column sitting on spare width.
    configureColumns(m_table, {0});
    sortAscendingBy(m_table, 0);
    m_table->setMinimumWidth(560);

    auto* directoryEmpty =
        new EmptyState(QStringLiteral("No customers yet"),
                       QStringLiteral("Guests you register at the till appear here with their "
                                      "visit count and loyalty balance."),
                       directory);
    QPushButton* emptyAdd = directoryEmpty->enableAction(QStringLiteral("Register the first guest"));

    auto* directoryStack = new QStackedWidget(directory);
    directoryStack->setObjectName(QStringLiteral("customersDirectoryStack"));
    directoryStack->addWidget(m_table);
    directoryStack->addWidget(directoryEmpty);
    directoryStack->setMinimumHeight(6 * kRowH);  // whole rows, never one sliced in half
    new WholeRowFitter(m_table, directoryStack);
    directoryCol->addWidget(directoryStack, 1);
    body->addWidget(directory, 3);

    // Guest-profile panel ----------------------------------------------------------------------
    auto* profile = makePanel(this);
    profile->setMinimumWidth(360);
    auto* profileCol = new QVBoxLayout(profile);
    profileCol->setContentsMargins(kPanelPad, kPanelPad, kPanelPad, kPanelPad);
    profileCol->setSpacing(12);
    profileCol->addWidget(makeLabel(QStringLiteral("Guest profile"),
                                    QStringLiteral("sectionTitle"), profile, 13, QFont::DemiBold));

    auto* profileName = makeLabel(QStringLiteral("No guest selected"), QString(), profile, 18,
                                  QFont::DemiBold);
    profileName->setObjectName(QStringLiteral("customerProfileName"));
    profileName->setWordWrap(true);
    profileCol->addWidget(profileName);

    auto* profileContact =
        makeLabel(QStringLiteral("Pick somebody from the directory to see their history."),
                  QStringLiteral("mutedLabel"), profile, 11);
    profileContact->setProperty("aluchopRole", QStringLiteral("customerProfileContact"));
    profileContact->setWordWrap(true);
    profileCol->addWidget(profileContact);

    auto* chips = new QHBoxLayout;
    chips->setSpacing(8);
    auto* visitsChip = makeChip(QStringLiteral("0 visits"), profile);
    visitsChip->setObjectName(QStringLiteral("customerVisitsChip"));
    auto* pointsChip = makeChip(QStringLiteral("0 points"), profile);
    pointsChip->setObjectName(QStringLiteral("customerPointsChip"));
    chips->addWidget(visitsChip);
    chips->addWidget(pointsChip);
    chips->addStretch(1);
    profileCol->addLayout(chips);

    profileCol->addWidget(makeLabel(QStringLiteral("FAVOURITE ORDERS"),
                                    QStringLiteral("mutedLabel"), profile, 10, QFont::DemiBold));
    m_favourites = makeLabel(QStringLiteral("—"), QString(), profile, 12);
    m_favourites->setObjectName(QStringLiteral("customerFavourites"));
    m_favourites->setWordWrap(true);
    profileCol->addWidget(m_favourites);

    // The visit counter and the itemised history are two different measurements of the same
    // guest, and the screen used to let them contradict each other ("3 visits" beside "has not
    // been served here so far"). They are reconciled in one place instead: this caption states
    // how many of the counted visits have an itemised bill behind them.
    auto* historyHead = new QHBoxLayout;
    historyHead->setSpacing(8);
    historyHead->addWidget(makeLabel(QStringLiteral("VISIT HISTORY"),
                                     QStringLiteral("mutedLabel"), profile, 10, QFont::DemiBold));
    historyHead->addStretch(1);
    auto* historyCount = makeLabel(QString(), QStringLiteral("mutedLabel"), profile, 10);
    historyCount->setObjectName(QStringLiteral("customerHistoryCount"));
    historyHead->addWidget(historyCount);
    profileCol->addLayout(historyHead);

    m_history = new ElegantTable(QStringList{QStringLiteral("Date"), QStringLiteral("Order"),
                                             QStringLiteral("Items"), QStringLiteral("Total")},
                                 profile);
    m_history->setSortingEnabled(false);
    m_history->verticalHeader()->setDefaultSectionSize(38);
    // "Items" is the only free-text column, so it is the only one allowed to stretch; the order
    // number is an identifier and sizes to itself rather than being elided.
    configureColumns(m_history, {2});

    auto* historyEmpty = new EmptyState(QStringLiteral("No visits recorded"),
                                        QStringLiteral("Settled orders appear here."), profile);
    auto* historyStack = new QStackedWidget(profile);
    historyStack->setObjectName(QStringLiteral("customersHistoryStack"));
    historyStack->addWidget(m_history);
    historyStack->addWidget(historyEmpty);
    historyStack->setMinimumHeight(4 * kRowH);
    new WholeRowFitter(m_history, historyStack);
    profileCol->addWidget(historyStack, 1);

    auto* profileActions = new QHBoxLayout;
    profileActions->setSpacing(8);
    auto* redeemBtn = makeButton(QStringLiteral("Redeem points"), QStringLiteral("ghostButton"),
                                 profile);
    redeemBtn->setObjectName(QStringLiteral("ghostButton"));
    profileActions->addWidget(redeemBtn);
    profileActions->addStretch(1);
    profileCol->addLayout(profileActions);

    body->addWidget(profile, 2);
    page->addLayout(body, 1);

    // --- wiring ---------------------------------------------------------------------------
    connect(searchBtn, &QPushButton::clicked, this, &CustomersPage::onSearch);
    connect(m_search, &QLineEdit::returnPressed, this, &CustomersPage::onSearch);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) { onSearch(); });
    connect(addBtn, &QPushButton::clicked, this, &CustomersPage::onAdd);
    connect(emptyAdd, &QPushButton::clicked, this, &CustomersPage::onAdd);
    connect(editBtn, &QPushButton::clicked, this, &CustomersPage::onEdit);
    connect(m_table, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*) { onEdit(); });
    connect(deleteBtn, &QPushButton::clicked, this, &CustomersPage::onDelete);
    // The profile panel must follow the selection, however the selection moved — a click, a
    // keyboard arrow, or a programmatic selectRow() after a search. Both signals are wired
    // because neither alone covers all three, and onShowHistory() is idempotent.
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (BusyGuard::busy(this)) return;  // ignore the churn a repopulation causes
        onShowHistory();
    });
    connect(m_table, &QTableWidget::currentCellChanged, this,
            [this](int, int, int, int) {
                if (BusyGuard::busy(this)) return;
                onShowHistory();
            });
    connect(redeemBtn, &QPushButton::clicked, this, [this]() {
        const int id = selectedCustomerId();
        if (id == 0) {
            m_ctx.notifications().notify(QStringLiteral("Nobody selected"),
                                         QStringLiteral("Choose a guest in the directory first."),
                                         2);
            return;
        }
        const auto customer = m_ctx.customers().byId(id);
        if (!customer) return;

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Redeem loyalty points"));
        dialog.setMinimumWidth(360);
        auto* column = new QVBoxLayout(&dialog);
        column->setContentsMargins(20, 20, 20, 20);
        column->setSpacing(14);
        column->addWidget(makeLabel(customer->name(), QStringLiteral("sectionTitle"), &dialog, 15,
                                    QFont::DemiBold));
        column->addWidget(makeLabel(QStringLiteral("Balance: %1 points")
                                        .arg(customer->loyaltyPoints()),
                                    QStringLiteral("mutedLabel"), &dialog, 11));
        auto* form = new QFormLayout;
        form->setSpacing(10);
        auto* amount = new QSpinBox(&dialog);
        amount->setRange(1, std::max(1, customer->loyaltyPoints()));
        amount->setValue(std::min(100, std::max(1, customer->loyaltyPoints())));
        amount->setSuffix(QStringLiteral(" pts"));
        form->addRow(QStringLiteral("Redeem"), amount);
        column->addLayout(form);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                             &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Redeem"));
        buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primaryButton"));
        buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
        column->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (customer->loyaltyPoints() <= 0) {
            m_ctx.notifications().notify(QStringLiteral("No points to redeem"),
                                         QStringLiteral("%1 has an empty loyalty balance.")
                                             .arg(customer->name()),
                                         2);
            return;
        }
        if (dialog.exec() != QDialog::Accepted) return;

        const auto result = m_ctx.customers().redeemPoints(id, amount->value());
        if (result.isOk()) {
            m_ctx.notifications().notify(QStringLiteral("Points redeemed"),
                                         QStringLiteral("%1 points taken off %2's balance.")
                                             .arg(amount->value())
                                             .arg(customer->name()),
                                         1);
            refresh();
        } else {
            m_ctx.notifications().notify(QStringLiteral("Could not redeem"), result.error(), 3);
        }
    });

    // Light/Dark switches live: refresh() re-reads every colour from the palette.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &CustomersPage::refresh);

    new EntranceAnimator(this);
}

// ============================================================================================
//  Page interface
// ============================================================================================

QString CustomersPage::pageTitle() const { return QStringLiteral("Customers"); }

void CustomersPage::refresh() {
    // The Page contract says refresh() must be idempotent and must never throw: it is called
    // from signal handlers where an escaping exception would tear down the event loop.
    if (BusyGuard::busy(this)) return;
    BusyGuard guard(this);
    try {
        const Palette& p = theme();

        const int previousId = selectedCustomerId();
        const QString term = m_search ? m_search->text().trimmed() : QString();
        const std::vector<models::Customer> rows =
            term.isEmpty() ? m_ctx.customers().all() : m_ctx.customers().search(term);

        beginRepopulate(m_table);
        int rowIndex = 0;
        for (const models::Customer& customer : rows) {
            m_table->insertRow(rowIndex);

            auto* nameCell = textCell(customer.name());
            nameCell->setData(Qt::UserRole, customer.id());
            QFont nameFont = nameCell->font();
            nameFont.setWeight(QFont::DemiBold);
            nameCell->setFont(nameFont);
            m_table->setItem(rowIndex, 0, withTooltip(nameCell));

            m_table->setItem(rowIndex, 1,
                             textCell(customer.phone().isEmpty() ? QStringLiteral("—")
                                                                 : customer.phone()));
            m_table->setItem(rowIndex, 2,
                             withTooltip(textCell(customer.email().isEmpty()
                                                      ? QStringLiteral("—")
                                                      : customer.email())));

            auto* visitsCell = numberCell(QString::number(customer.visits()), customer.visits());
            m_table->setItem(rowIndex, 3, visitsCell);

            auto* pointsCell = numberCell(QStringLiteral("%1 pts").arg(customer.loyaltyPoints()),
                                          customer.loyaltyPoints());
            pointsCell->setForeground(customer.loyaltyPoints() > 0 ? p.primary : p.textMuted);
            m_table->setItem(rowIndex, 4, pointsCell);

            ++rowIndex;
        }
        endRepopulate(m_table);

        // Empty state instead of a bare grid.
        if (auto* stack = findChild<QStackedWidget*>(QStringLiteral("customersDirectoryStack"))) {
            stack->setCurrentIndex(rows.empty() ? 1 : 0);
            if (auto* empty = dynamic_cast<EmptyState*>(stack->widget(1))) {
                if (rows.empty() && !term.isEmpty()) {
                    empty->setText(QStringLiteral("No match for “%1”").arg(term),
                                   QStringLiteral("Try part of a name, a phone number or an "
                                                  "e-mail address."));
                } else if (rows.empty()) {
                    empty->setText(QStringLiteral("No customers yet"),
                                   QStringLiteral("Guests you register at the till appear here "
                                                  "with their visit count and loyalty balance."));
                }
            }
        }

        // Restore the previous selection so a refresh does not steal the user's place.
        if (previousId != 0) {
            for (int row = 0; row < m_table->rowCount(); ++row) {
                const QTableWidgetItem* cell = m_table->item(row, 0);
                if (cell && cell->data(Qt::UserRole).toInt() == previousId) {
                    m_table->selectRow(row);
                    break;
                }
            }
        } else if (!property("aluchopPrimed").toBool() && m_table->rowCount() > 0) {
            // First time this screen is filled, open on the first guest: the profile panel then
            // shows a real visit count and a real points balance instead of two dashes.
            setProperty("aluchopPrimed", true);
            m_table->selectRow(0);
        }

        // Header subtitle: a quiet running total.
        for (QLabel* label : findChildren<QLabel*>()) {
            if (label->property("aluchopRole").toString() == QLatin1String("customersSubtitle")) {
                int totalPoints = 0;
                int totalVisits = 0;
                for (const models::Customer& customer : rows) {
                    totalPoints += customer.loyaltyPoints();
                    totalVisits += customer.visits();
                }
                label->setText(QStringLiteral("%1 guest%2  ·  %3 recorded visits  ·  %4 loyalty "
                                              "points outstanding")
                                   .arg(rows.size())
                                   .arg(rows.size() == 1 ? QString() : QStringLiteral("s"))
                                   .arg(totalVisits)
                                   .arg(totalPoints));
            }
        }

        onShowHistory();
    } catch (const std::exception& e) {
        m_ctx.notifications().notify(QStringLiteral("Customers"),
                                     QString::fromUtf8(e.what()), 3);
    }
}

// ============================================================================================
//  Slots
// ============================================================================================

void CustomersPage::onSearch() { refresh(); }

void CustomersPage::onAdd() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("New customer"));
    dialog.setMinimumWidth(420);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(22, 22, 22, 22);
    column->setSpacing(16);
    column->addWidget(makeLabel(QStringLiteral("Register a guest"), QStringLiteral("sectionTitle"),
                                &dialog, 16, QFont::DemiBold));
    column->addWidget(makeLabel(QStringLiteral("The phone number is the key used at the till, so "
                                               "it has to be unique."),
                                QStringLiteral("mutedLabel"), &dialog, 11));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    auto* name = new QLineEdit(&dialog);
    name->setMinimumHeight(kControlH);
    name->setPlaceholderText(QStringLiteral("Full name"));
    auto* phone = new QLineEdit(&dialog);
    phone->setMinimumHeight(kControlH);
    phone->setPlaceholderText(QStringLiteral("98XXXXXXXX"));
    auto* email = new QLineEdit(&dialog);
    email->setMinimumHeight(kControlH);
    email->setPlaceholderText(QStringLiteral("optional"));
    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Phone"), phone);
    form->addRow(QStringLiteral("E-mail"), email);
    column->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Register"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primaryButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    const auto created = m_ctx.customers().create(name->text().trimmed(),
                                                  phone->text().trimmed(),
                                                  email->text().trimmed());
    if (created.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Customer registered"),
                                     QStringLiteral("%1 is now in the loyalty programme.")
                                         .arg(name->text().trimmed()),
                                     1);
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not register"), created.error(), 3);
    }
}

void CustomersPage::onEdit() {
    const int id = selectedCustomerId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Nobody selected"),
                                     QStringLiteral("Choose a guest in the directory first."), 2);
        return;
    }
    const auto existing = m_ctx.customers().byId(id);
    if (!existing) {
        m_ctx.notifications().notify(QStringLiteral("Customer missing"),
                                     QStringLiteral("That record is no longer in the database."),
                                     3);
        refresh();
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit customer"));
    dialog.setMinimumWidth(420);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(22, 22, 22, 22);
    column->setSpacing(16);
    column->addWidget(makeLabel(QStringLiteral("Edit %1").arg(existing->name()),
                                QStringLiteral("sectionTitle"), &dialog, 16, QFont::DemiBold));
    column->addWidget(makeLabel(QStringLiteral("Visits and loyalty points are earned, never typed "
                                               "in — they are maintained by the billing flow."),
                                QStringLiteral("mutedLabel"), &dialog, 11));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    auto* name = new QLineEdit(existing->name(), &dialog);
    name->setMinimumHeight(kControlH);
    auto* phone = new QLineEdit(existing->phone(), &dialog);
    phone->setMinimumHeight(kControlH);
    auto* email = new QLineEdit(existing->email(), &dialog);
    email->setMinimumHeight(kControlH);
    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Phone"), phone);
    form->addRow(QStringLiteral("E-mail"), email);
    form->addRow(QStringLiteral("Visits"),
                 makeLabel(QString::number(existing->visits()), QString(), &dialog, 12));
    form->addRow(QStringLiteral("Loyalty"),
                 makeLabel(QStringLiteral("%1 points").arg(existing->loyaltyPoints()), QString(),
                           &dialog, 12));
    column->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primaryButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    models::Customer edited = *existing;
    try {
        // Model setters are the validation gate; a bad value throws before it can reach SQLite.
        edited.setName(name->text().trimmed());
        edited.setPhone(phone->text().trimmed());
        edited.setEmail(email->text().trimmed());
    } catch (const core::ValidationException& e) {
        m_ctx.notifications().notify(QStringLiteral("Invalid details"),
                                     QString::fromUtf8(e.what()), 3);
        return;
    }

    const auto saved = m_ctx.customers().update(edited);
    if (saved.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Customer saved"),
                                     QStringLiteral("%1's details were updated.")
                                         .arg(edited.name()),
                                     1);
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not save"), saved.error(), 3);
    }
}

void CustomersPage::onDelete() {
    const int id = selectedCustomerId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Nobody selected"),
                                     QStringLiteral("Choose a guest in the directory first."), 2);
        return;
    }
    const auto existing = m_ctx.customers().byId(id);
    const QString who = existing ? existing->name() : QStringLiteral("this customer");

    QMessageBox confirm(this);
    confirm.setWindowTitle(QStringLiteral("Remove customer"));
    confirm.setIcon(QMessageBox::Warning);
    confirm.setText(QStringLiteral("Remove %1 from the loyalty programme?").arg(who));
    confirm.setInformativeText(QStringLiteral("Past orders keep their own name and price "
                                              "snapshots; only the link to this record is lost."));
    confirm.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.button(QMessageBox::Yes)->setText(QStringLiteral("Remove"));
    confirm.button(QMessageBox::Yes)->setObjectName(QStringLiteral("dangerButton"));
    confirm.button(QMessageBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    if (confirm.exec() != QMessageBox::Yes) return;

    const auto removed = m_ctx.customers().remove(id);
    if (removed.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Customer removed"),
                                     QStringLiteral("%1 is no longer in the directory.").arg(who),
                                     1);
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not remove"), removed.error(), 3);
    }
}

void CustomersPage::onShowHistory() {
    const Palette& p = theme();
    const int id = selectedCustomerId();

    auto* nameLabel = findChild<QLabel*>(QStringLiteral("customerProfileName"));
    QLabel* contactLabel = nullptr;
    for (QLabel* label : findChildren<QLabel*>()) {
        if (label->property("aluchopRole").toString() == QLatin1String("customerProfileContact")) {
            contactLabel = label;
            break;
        }
    }
    auto* visitsChip = findChild<QLabel*>(QStringLiteral("customerVisitsChip"));
    auto* pointsChip = findChild<QLabel*>(QStringLiteral("customerPointsChip"));
    auto* historyStack = findChild<QStackedWidget*>(QStringLiteral("customersHistoryStack"));
    auto* historyCount = findChild<QLabel*>(QStringLiteral("customerHistoryCount"));

    if (id == 0) {
        if (historyCount) historyCount->setText(QString());
        if (nameLabel) nameLabel->setText(QStringLiteral("No guest selected"));
        if (contactLabel) {
            contactLabel->setText(
                QStringLiteral("Pick somebody from the directory to see their history."));
        }
        // No guest is selected, so there is no visit count and no points balance to state. A
        // dash in the shape of a number would be a lie about the database — the chips are hidden
        // instead, and the panel says plainly that nothing is selected.
        if (visitsChip) visitsChip->setVisible(false);
        if (pointsChip) pointsChip->setVisible(false);
        if (m_favourites) {
            m_favourites->setText(QStringLiteral("Nothing to show until a guest is selected"));
            m_favourites->setStyleSheet(QStringLiteral("color: %1;").arg(p.textMuted.name()));
        }
        beginRepopulate(m_history);
        if (historyStack) {
            historyStack->setCurrentIndex(1);
            if (auto* empty = dynamic_cast<EmptyState*>(historyStack->widget(1))) {
                empty->setText(QStringLiteral("Nothing to show"),
                               QStringLiteral("Select a guest on the left."));
            }
        }
        return;
    }

    try {
        const auto customer = m_ctx.customers().byId(id);
        if (!customer) return;

        if (nameLabel) nameLabel->setText(customer->name());
        if (contactLabel) {
            QStringList parts;
            if (!customer->phone().isEmpty()) parts << customer->phone();
            if (!customer->email().isEmpty()) parts << customer->email();
            if (customer->createdAt().isValid()) {
                parts << QStringLiteral("member since %1")
                             .arg(customer->createdAt().toLocalTime().toString(
                                 QStringLiteral("MMM yyyy")));
            }
            contactLabel->setText(parts.isEmpty() ? QStringLiteral("No contact details on file")
                                                  : parts.join(QStringLiteral("  ·  ")));
        }
        if (visitsChip) {
            visitsChip->setVisible(true);
            visitsChip->setText(QStringLiteral("%1 visit%2")
                                    .arg(customer->visits())
                                    .arg(customer->visits() == 1 ? QString()
                                                                 : QStringLiteral("s")));
            visitsChip->setToolTip(
                QStringLiteral("Lifetime visit counter — the value models::Customer::operator++ "
                               "advances every time a bill is settled for %1.")
                    .arg(customer->name()));
            styleChip(visitsChip, customer->visits() > 0 ? p.secondary : p.textMuted);
        }
        if (pointsChip) {
            pointsChip->setVisible(true);
            pointsChip->setText(QStringLiteral("%1 points").arg(customer->loyaltyPoints()));
            styleChip(pointsChip, customer->loyaltyPoints() > 0 ? p.success : p.textMuted);
        }

        // Favourites — computed by the service (std::map tally + std::sort), shown verbatim.
        const std::vector<QString> favourites = m_ctx.customers().favouriteItems(id, 3);
        if (m_favourites) {
            if (favourites.empty()) {
                m_favourites->setText(QStringLiteral("Nothing ordered yet"));
                m_favourites->setStyleSheet(QStringLiteral("color: %1;").arg(p.textMuted.name()));
            } else {
                QStringList shown;
                for (const QString& dish : favourites) shown << dish;
                m_favourites->setText(shown.join(QStringLiteral("   ·   ")));
                m_favourites->setStyleSheet(QStringLiteral("color: %1;").arg(p.text.name()));
            }
        }

        // Visit history — the service's default limit of 20 is exactly the panel's appetite.
        const std::vector<models::Order> history = m_ctx.customers().visitHistory(id);
        beginRepopulate(m_history);
        int rowIndex = 0;
        for (const models::Order& order : history) {
            m_history->insertRow(rowIndex);
            // The clock time is the least meaningful thing in the row, so it rides on the tooltip
            // and the width it was taking goes to the dish names instead.
            const QDateTime when = order.createdAt().toLocalTime();
            auto* dateCell = textCell(when.toString(QStringLiteral("dd MMM yyyy")));
            dateCell->setToolTip(when.toString(QStringLiteral("dddd dd MMMM yyyy 'at' HH:mm")));
            m_history->setItem(rowIndex, 0, dateCell);
            m_history->setItem(rowIndex, 1,
                               withTooltip(textCell(order.orderNumber().isEmpty()
                                                        ? QStringLiteral("—")
                                                        : order.orderNumber())));

            QStringList dishes;
            int units = 0;
            for (const models::OrderItem& line : order.items()) {
                units += line.qty();
                if (dishes.size() < 3) {
                    dishes << QStringLiteral("%1 ×%2").arg(line.name()).arg(line.qty());
                }
            }
            QString summary = dishes.join(QStringLiteral(", "));
            if (order.itemCount() > 3) {
                summary += QStringLiteral(" +%1 more")
                               .arg(static_cast<int>(order.itemCount()) - 3);
            }
            if (summary.isEmpty()) summary = QStringLiteral("—");
            auto* itemsCell = textCell(summary);
            itemsCell->setToolTip(QStringLiteral("%1 units across %2 line%3")
                                      .arg(units)
                                      .arg(order.itemCount())
                                      .arg(order.itemCount() == 1 ? QString()
                                                                  : QStringLiteral("s")));
            m_history->setItem(rowIndex, 2, itemsCell);

            const Money total = order.subtotal();
            auto* totalCell = numberCell(core::formatNpr(total),
                                         static_cast<double>(total.paisa()));
            m_history->setItem(rowIndex, 3, totalCell);
            ++rowIndex;
        }

        // One reconciliation, two widgets. `visits` is the lifetime counter on the customer row;
        // `itemised` is how many of those visits still have their order lines in this database.
        // Every sentence below is derived from those two numbers, so the caption, the chip and the
        // empty state cannot disagree about the same guest again.
        const int visits = customer->visits();
        const int itemised = static_cast<int>(history.size());
        if (historyCount) {
            historyCount->setText(
                visits == 0 ? QStringLiteral("no visits on record")
                            : QStringLiteral("%1 of %2 itemised").arg(itemised).arg(visits));
            historyCount->setToolTip(
                QStringLiteral("%1 has %2 recorded visit(s). %3 of them still have their order "
                               "lines stored, and those are the ones listed here.")
                    .arg(customer->name())
                    .arg(visits)
                    .arg(itemised));
        }

        if (historyStack) {
            historyStack->setCurrentIndex(history.empty() ? 1 : 0);
            if (auto* empty = dynamic_cast<EmptyState*>(historyStack->widget(1))) {
                if (visits > 0) {
                    empty->setText(
                        QStringLiteral("%1 visit%2, none itemised")
                            .arg(visits)
                            .arg(visits == 1 ? QString() : QStringLiteral("s")),
                        QStringLiteral("%1's visit counter and loyalty balance were carried over "
                                       "when the book was opened, so there are no order lines to "
                                       "list yet. The next settled bill appears here.")
                            .arg(customer->name()));
                } else {
                    empty->setText(QStringLiteral("No visits yet"),
                                   QStringLiteral("%1 has not been served here so far.")
                                       .arg(customer->name()));
                }
            }
        }
    } catch (const std::exception& e) {
        m_ctx.notifications().notify(QStringLiteral("Customer history"),
                                     QString::fromUtf8(e.what()), 3);
    }
}

// ============================================================================================
//  Helpers
// ============================================================================================

int CustomersPage::selectedCustomerId() const {
    if (!m_table) return 0;
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_table->rowCount()) return 0;
    if (!m_table->selectionModel() || !m_table->selectionModel()->hasSelection()) return 0;
    const QTableWidgetItem* cell = m_table->item(row, 0);
    return cell ? cell->data(Qt::UserRole).toInt() : 0;
}

}  // namespace aluchop::gui

/**
 * @file DashboardPage.cpp
 * @brief Screen 1 — animated statistics, revenue chart, alerts and pending orders.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The dashboard is a pure *reader*: every number on it is produced by a service
 * (services::ReportService, services::InventoryService, services::ReservationService,
 * services::OrderService) and this file only decides where it sits and how it animates.
 * There is no aggregation, no filtering and — by architectural rule — no SQL here.
 */

#include "aluchop/gui/DashboardPage.hpp"

#include <QAbstractScrollArea>
#include <QDate>
#include <QDateTime>
#include <QEasingCurve>
#include <QFont>
#include <QColor>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLayoutItem>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMargins>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSize>
#include <QSizePolicy>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QVariantAnimation>

#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLegend>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <vector>

#include "aluchop/core/Money.hpp"
#include "aluchop/gui/ChartKit.hpp"
#include "aluchop/gui/StatCard.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/gui/Widgets.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/Ingredient.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/models/Reservation.hpp"
#include "aluchop/models/Table.hpp"
#include "aluchop/services/AppContext.hpp"

namespace aluchop::gui {
namespace {

// ---------------------------------------------------------------------------
// Small presentation helpers. Colours are *always* taken from the live Palette
// so that a theme switch repaints everything (SPEC §1).
// ---------------------------------------------------------------------------

/// Plot height, and the band the two list panels and the order table are allowed to occupy.
/// They are sized to whole rows inside these bounds so nothing is ever sliced in half.
///
/// These numbers are a *budget*, not decoration. The screen is four panels in three bands, and at
/// the shell's ordinary size the three bands plus their spacing have to add up to less than the
/// viewport — otherwise the dashboard scrolls and its own live order board sits below the fold,
/// which is the failure this pass exists to remove. A panel is ~88 px of heading, subtitle and
/// padding around whichever of these heights it carries.
constexpr int kChartHeight = 190;      ///< a *floor*; the plot then fills whatever its band gives it
constexpr int kRowContentHeight = 30;  ///< minimum height of a list row's *content*
constexpr int kListMinHeight = 165;
constexpr int kListMaxHeight = 220;    ///< four whole rows — the alert feed's natural size
constexpr int kTableMinHeight = 176;   ///< the 44 px header plus three whole 44 px rows

/// Applies a point-size delta and a weight to a label and gives it a QSS object name.
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

/// A small round colour swatch used as a list-row bullet (status / severity).
QIcon bulletIcon(const QColor& colour) {
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    QColor halo = colour;
    halo.setAlpha(70);
    p.setBrush(halo);
    p.drawEllipse(0, 0, 14, 14);
    p.setBrush(colour);
    p.drawEllipse(4, 4, 6, 6);
    p.end();
    return QIcon(pm);
}

/// Human label for an order status.
QString statusText(models::OrderStatus s) {
    switch (s) {
        case models::OrderStatus::Open:      return QStringLiteral("Open");
        case models::OrderStatus::Pending:   return QStringLiteral("Pending");
        case models::OrderStatus::Preparing: return QStringLiteral("Preparing");
        case models::OrderStatus::Ready:     return QStringLiteral("Ready");
        case models::OrderStatus::Served:    return QStringLiteral("Served");
        case models::OrderStatus::Paid:      return QStringLiteral("Paid");
        case models::OrderStatus::Cancelled: return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Unknown");
}

/// Palette colour that carries the meaning of a status.
QColor statusColour(models::OrderStatus s, const Palette& p) {
    switch (s) {
        case models::OrderStatus::Open:      return p.textMuted;
        case models::OrderStatus::Pending:   return p.secondary;
        case models::OrderStatus::Preparing: return p.primary;
        case models::OrderStatus::Ready:     return p.success;
        case models::OrderStatus::Served:    return p.primary;
        case models::OrderStatus::Paid:      return p.textMuted;
        case models::OrderStatus::Cancelled: return p.danger;
    }
    return p.text;
}

/// Human label for an order type.
QString typeText(models::OrderType t) {
    switch (t) {
        case models::OrderType::DineIn:   return QStringLiteral("Dine-In");
        case models::OrderType::Takeaway: return QStringLiteral("Takeaway");
        case models::OrderType::Delivery: return QStringLiteral("Delivery");
    }
    return QString();
}

/**
 * @brief A grid of equal cards that re-flows its column count with the available width.
 *
 * This is what makes the dashboard responsive instead of a fixed pixel layout: the cards
 * are laid out 4-across on a wide window, 2-across on a laptop half-screen and 1-across on
 * a narrow one, purely from the measured width.
 *
 * @oop-concept Method Overriding :: resizeEvent() is QWidget's hook; overriding it is how a
 *              layout becomes responsive without any timer or manual geometry code
 */
class CardGrid : public QWidget {
public:
    explicit CardGrid(int minCardWidth, QWidget* parent = nullptr)
        : QWidget(parent), m_minCardWidth(minCardWidth) {
        m_grid = new QGridLayout(this);
        m_grid->setContentsMargins(0, 0, 0, 0);
        m_grid->setSpacing(18);
        setAutoFillBackground(false);
    }

    /// Adds a card; the widget is reparented and immediately placed.
    void addCard(QWidget* card) {
        card->setParent(this);
        m_cards.push_back(card);
        relayout(columnsForWidth(width()));
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        const int cols = columnsForWidth(event->size().width());
        if (cols != m_columns) relayout(cols);
    }

private:
    int columnsForWidth(int w) const {
        if (m_cards.empty()) return 1;
        const int spacing = m_grid->spacing();
        int cols = (w + spacing) / (m_minCardWidth + spacing);
        cols = std::max(1, cols);
        return std::min(cols, static_cast<int>(m_cards.size()));
    }

    void relayout(int columns) {
        m_columns = std::max(1, columns);
        while (QLayoutItem* item = m_grid->takeAt(0)) delete item;  // frees items, not widgets
        for (int c = 0; c < 8; ++c) m_grid->setColumnStretch(c, c < m_columns ? 1 : 0);
        for (std::size_t i = 0; i < m_cards.size(); ++i) {
            const int idx = static_cast<int>(i);
            m_grid->addWidget(m_cards[i], idx / m_columns, idx % m_columns);
        }
    }

    QGridLayout* m_grid = nullptr;
    std::vector<QWidget*> m_cards;
    int m_minCardWidth = 240;
    int m_columns = 0;
};

/// Builds an opaque card surface with a heading and returns the body layout to fill.
QVBoxLayout* buildPanel(const QString& title, const QString& subtitle, QWidget* parent,
                        QFrame** outPanel) {
    auto* panel = new GlassPanel(parent);
    panel->setObjectName(QStringLiteral("card"));
    auto* column = new QVBoxLayout(panel);
    column->setContentsMargins(18, 16, 18, 16);
    column->setSpacing(10);

    auto* heading = styledLabel(title, QStringLiteral("sectionTitle"), panel, 1, QFont::DemiBold);
    column->addWidget(heading);
    if (!subtitle.isEmpty()) {
        auto* sub = styledLabel(subtitle, QStringLiteral("mutedLabel"), panel, -1);
        sub->setWordWrap(true);
        column->addWidget(sub);
    }
    *outPanel = panel;
    return column;
}

/// A two-part list row: a leading label and a trailing, right-aligned value.
QWidget* listRow(const QString& lead, const QString& trail, const QColor& trailColour,
                 QWidget* parent) {
    auto* row = new QWidget(parent);
    row->setAutoFillBackground(false);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(2, 4, 2, 4);
    h->setSpacing(12);

    auto* leadLabel = new QLabel(lead, row);
    leadLabel->setWordWrap(false);
    leadLabel->setTextInteractionFlags(Qt::NoTextInteraction);

    auto* trailLabel = new QLabel(trail, row);
    QFont tf = trailLabel->font();
    tf.setWeight(QFont::DemiBold);
    trailLabel->setFont(tf);
    QPalette tp = trailLabel->palette();
    tp.setColor(QPalette::WindowText, trailColour);
    trailLabel->setPalette(tp);
    trailLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    h->addWidget(leadLabel, 1);
    h->addWidget(trailLabel, 0);
    return row;
}

/// @return the vertical space a list row loses to the stylesheet's own item padding and margin.
///
/// ThemeManager styles `QListWidget::item` with padding, and Qt places an item *widget* inside
/// that padded rectangle. A row whose size hint is only as tall as its content therefore leaves
/// the widget squeezed into what is left — 14 px of a 34 px row — and its text sliced through the
/// middle. The live style is asked how much a row loses, so the answer follows the stylesheet
/// instead of hardcoding a copy of it.
int itemChromeHeight(const QListWidget* list) {
    QStyleOptionViewItem option;
    option.initFrom(list);
    option.rect = QRect(0, 0, 300, 100);
    const int text =
        list->style()->subElementRect(QStyle::SE_ItemViewItemText, &option, list).height();
    return std::max(0, 100 - text);
}

/// Adds a rich row (leading text + trailing value) to a list widget.
void addRichRow(QListWidget* list, const QIcon& icon, const QString& lead, const QString& trail,
                const QColor& trailColour) {
    auto* item = new QListWidgetItem(list);
    auto* row = listRow(lead, trail, trailColour, list);
    if (!icon.isNull()) {
        // The bullet lives on the item so the row widget stays a clean two-column layout.
        item->setIcon(icon);
        row->layout()->setContentsMargins(24, 4, 2, 4);
    }
    // Content height plus the padding the row will be inset by — a row is never shorter than what
    // it has to show.
    const int content = std::max(row->sizeHint().height(), kRowContentHeight);
    item->setSizeHint(QSize(0, content + itemChromeHeight(list)));
    list->setItemWidget(item, row);
}

/// Shows a centred "nothing here" line inside an otherwise empty list.
void addEmptyRow(QListWidget* list, const QString& text, const QColor& colour) {
    auto* item = new QListWidgetItem(text, list);
    item->setFlags(Qt::NoItemFlags);
    item->setTextAlignment(Qt::AlignCenter);
    item->setForeground(colour);
    item->setSizeHint(QSize(0, 64));
}

/// Right-aligned, non-editable money cell.
QTableWidgetItem* moneyCell(core::Money m) {
    auto* cell = new QTableWidgetItem(core::formatNpr(m));
    cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return cell;
}

/// Object name of the count-up animations this screen installs on a card, so a new count-up
/// replaces only its own predecessor. Stopping *every* QVariantAnimation on the tile — as this
/// used to — also killed the card's entrance fade and rise, which are QVariantAnimations too.
const QString kCountUpName = QStringLiteral("dashboardCountUp");

/**
 * @brief Counts a number up from zero into a StatCard.
 *
 * The card itself only knows how to display a string (StatCard::setValue), so the count-up is
 * expressed here as an animation over a plain double that is formatted on every tick. Money is
 * animated in *paisa* and formatted through core::formatNpr, so no currency ever becomes a
 * double outside this one presentation step.
 */
void countUp(StatCard* card, double target, const std::function<QString(double)>& format,
             int delayMs) {
    QTimer::singleShot(delayMs, card, [card, target, format]() {
        if (auto* previous =
                card->findChild<QVariantAnimation*>(kCountUpName, Qt::FindDirectChildrenOnly)) {
            previous->setObjectName(QString());  // so it can never be found twice
            previous->stop();                    // settles on its figure, then DeleteWhenStopped
        }

        // A refresh that lands on the number already displayed must not count it up again: the
        // headline would spend the whole animation disagreeing with its own subtitle, which
        // states the final figure immediately ("2" over "3 need reordering"). Stopping the
        // previous count-up above has already settled the label, so an equal string means there
        // is genuinely nothing to animate.
        if (const auto* label = card->findChild<QLabel*>(QStringLiteral("statCardValue"))) {
            if (label->text() == format(target)) {
                return;
            }
        }

        auto* anim = new QVariantAnimation(card);
        anim->setObjectName(kCountUpName);
        anim->setDuration(700);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->setStartValue(0.0);
        anim->setEndValue(target);
        QObject::connect(anim, &QVariantAnimation::valueChanged, card,
                         [card, format](const QVariant& v) { card->setValue(format(v.toDouble())); });
        // However the count-up ends — finished, or cancelled by the next refresh — the exact
        // figure is what stays on the tile. A cancelled count-up used to freeze at whatever tick
        // it had reached, which is how a card could read "2" while its own subtitle, built from
        // the very same query, said "3 need reordering".
        QObject::connect(anim, &QVariantAnimation::stateChanged, card,
                         [card, target, format](QAbstractAnimation::State state,
                                                QAbstractAnimation::State) {
                             if (state == QAbstractAnimation::Stopped) {
                                 card->setValue(format(target));
                             }
                         });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

/// The meaning a trend line carries. Neutral is the important one: a line that states a fact
/// rather than a direction must not be painted in Palette::danger (SPEC §1 — red means a problem).
enum class Tone { Positive, Neutral, Negative };

/// Sets a card's trend line together with its tone. StatCard::setDelta reads the `deltaTone`
/// dynamic property; see the comment there for why the tone cannot travel through its bool.
void setTrendLine(StatCard* card, const QString& text, Tone tone) {
    card->setProperty("deltaTone", tone == Tone::Positive   ? QStringLiteral("positive")
                                   : tone == Tone::Negative ? QStringLiteral("negative")
                                                            : QStringLiteral("neutral"));
    card->setDelta(text, tone == Tone::Positive);
}

/// @return the height at which a row view shows only *whole* rows, never exceeding @p maxH.
///
/// A view whose height is not a whole number of rows slices its last row in half — that is the
/// "clipped mid-height" look. Rows past @p maxH are reached by scrolling, with the last visible
/// one still complete.
int wholeRowsHeight(const std::vector<int>& rowHeights, int chromeH, int maxH) {
    int height = chromeH;
    for (int rowH : rowHeights) {
        if (height + rowH > maxH) break;
        height += rowH;
    }
    return height;
}

/// Pins a view to an exact height, so no surrounding stretch can squeeze it back into half a row.
void pinHeight(QAbstractScrollArea* view, int height) {
    view->setMinimumHeight(height);
    view->setMaximumHeight(height);
}

/// @return the row heights of a list widget, in order.
std::vector<int> listRowHeights(QListWidget* list) {
    std::vector<int> heights;
    heights.reserve(static_cast<std::size_t>(list->count()));
    for (int i = 0; i < list->count(); ++i) heights.push_back(list->sizeHintForRow(i));
    return heights;
}

/// Money formatter for the count-up (input is a paisa count).
QString formatPaisa(double paisa) {
    return core::formatNpr(core::Money(static_cast<std::int64_t>(std::llround(paisa))));
}

/// Whole-number formatter for the count-up.
QString formatCount(double v) { return QString::number(static_cast<long long>(std::llround(v))); }

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

DashboardPage::DashboardPage(services::AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    setObjectName(QStringLiteral("dashboardPage"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(18);

    // --- page header -------------------------------------------------------
    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(12);

    auto* headings = new QVBoxLayout();
    headings->setSpacing(2);
    headings->addWidget(styledLabel(pageTitle(), QStringLiteral("pageTitle"), this, 10,
                                    QFont::DemiBold));
    headings->addWidget(styledLabel(QDate::currentDate().toString(QStringLiteral("dddd, d MMMM yyyy")),
                                    QStringLiteral("mutedLabel"), this, 0));
    headerRow->addLayout(headings, 1);

    auto* refreshBtn = new QPushButton(QStringLiteral("Refresh"), this);
    refreshBtn->setObjectName(QStringLiteral("ghostButton"));
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setToolTip(QStringLiteral("Re-read every service (F5)"));
    connect(refreshBtn, &QPushButton::clicked, this, &DashboardPage::refresh);
    headerRow->addWidget(refreshBtn, 0, Qt::AlignVCenter);

    root->addLayout(headerRow);

    // --- scrolling body ----------------------------------------------------
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);

    auto* body = new QWidget(scroll);
    body->setAutoFillBackground(false);
    auto* bodyColumn = new QVBoxLayout(body);
    // Every panel is a GlassPanel and paints a soft drop shadow *outside* its own rectangle, so
    // the column needs a margin all round — otherwise the shadow of the last panel is shaved off
    // by the viewport edge and the row reads as clipped.
    bodyColumn->setContentsMargins(6, 4, 10, 8);
    bodyColumn->setSpacing(16);

    // --- four headline metrics --------------------------------------------
    auto* statGrid = new CardGrid(230, body);
    m_cards[0] = new StatCard(QStringLiteral("Today's Sales"),
                              QStringLiteral("assets/icons/sales.svg"), statGrid);
    m_cards[1] = new StatCard(QStringLiteral("Pending Orders"),
                              QStringLiteral("assets/icons/orders.svg"), statGrid);
    m_cards[2] = new StatCard(QStringLiteral("Customers"),
                              QStringLiteral("assets/icons/customers.svg"), statGrid);
    m_cards[3] = new StatCard(QStringLiteral("Low Stock Items"),
                              QStringLiteral("assets/icons/inventory.svg"), statGrid);
    for (StatCard* card : m_cards) {
        card->setMinimumHeight(118);
        statGrid->addCard(card);
    }
    bodyColumn->addWidget(statGrid);

    // --- revenue chart beside the best sellers -----------------------------
    // The chart used to be a full-width band 1300 px across carrying seven bars, which pushed the
    // popular-items list, the alert list and the entire live order board below the fold. Landscape
    // space is what this window has most of: pairing the chart with a list on each of two bands
    // fills the width with information and lifts every panel above the fold.
    auto* chartRow = new QHBoxLayout();
    chartRow->setSpacing(16);

    QFrame* chartPanel = nullptr;
    QVBoxLayout* chartColumn = buildPanel(QStringLiteral("Revenue — last 7 days"),
                                          QStringLiteral("Settled payments only"), body,
                                          &chartPanel);
    m_revenueChart = new QChartView(chartPanel);
    m_revenueChart->setObjectName(QStringLiteral("revenueChart"));
    m_revenueChart->setRenderHint(QPainter::Antialiasing, true);
    m_revenueChart->setFrameShape(QFrame::NoFrame);
    // Tall enough that seven bars, their value axis and the day labels all get room; a shorter
    // plot area is where axis labels start colliding with the bars.
    m_revenueChart->setMinimumHeight(kChartHeight);
    m_revenueChart->setBackgroundBrush(Qt::NoBrush);
    m_revenueChart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    chartColumn->addWidget(m_revenueChart, 1);

    // A week with no settled payments has nothing to plot: an axis grid with no bars reads as a
    // broken chart, so the chart steps aside for a proper empty state of exactly the same height
    // (the panel must not jump as the first payment arrives). Found later by object name, the way
    // the subtitle already is, because the frozen header has no member to hold it.
    auto* chartEmpty = new EmptyState(QStringLiteral("No revenue in the last 7 days"),
                                      QStringLiteral("Settled payments appear here as soon as a "
                                                     "bill is paid on the Orders screen."),
                                      chartPanel);
    chartEmpty->setMinimumHeight(kChartHeight);
    chartEmpty->setVisible(false);
    chartColumn->addWidget(chartEmpty, 1);

    chartRow->addWidget(chartPanel, 3);

    QFrame* popularPanel = nullptr;
    QVBoxLayout* popularColumn = buildPanel(QStringLiteral("Popular Items"),
                                            QStringLiteral("Best sellers of the last 30 days"),
                                            body, &popularPanel);
    m_popularItems = new QListWidget(popularPanel);
    m_popularItems->setObjectName(QStringLiteral("popularItems"));
    m_popularItems->setFrameShape(QFrame::NoFrame);
    m_popularItems->setSelectionMode(QAbstractItemView::NoSelection);
    m_popularItems->setFocusPolicy(Qt::NoFocus);
    m_popularItems->setMinimumHeight(kListMinHeight);
    popularColumn->addWidget(m_popularItems, 1);
    chartRow->addWidget(popularPanel, 2);

    bodyColumn->addLayout(chartRow);

    // --- live order board beside the alert feed ----------------------------
    auto* boardRow = new QHBoxLayout();
    boardRow->setSpacing(16);

    QFrame* ordersPanel = nullptr;
    QVBoxLayout* ordersColumn = buildPanel(QStringLiteral("Live Orders"),
                                           QStringLiteral("Everything currently open on the floor and in the kitchen"),
                                           body, &ordersPanel);
    m_pendingOrders = new ElegantTable(
        QStringList{QStringLiteral("Order"), QStringLiteral("Type"), QStringLiteral("Table"),
                    QStringLiteral("Status"), QStringLiteral("Total")},
        ordersPanel);
    m_pendingOrders->setSortingEnabled(false);
    m_pendingOrders->setMinimumHeight(kTableMinHeight);
    // The order number is what a waiter reads the row by, so it takes the slack and can never be
    // elided; the four short columns take exactly the width their own contents need. Stretching
    // "Status" instead — which is what this did — put the spare width in the middle of the row and
    // left the identifier squeezed against the left edge.
    m_pendingOrders->horizontalHeader()->setStretchLastSection(false);
    m_pendingOrders->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < m_pendingOrders->columnCount(); ++c)
        m_pendingOrders->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    ordersColumn->addWidget(m_pendingOrders, 1);
    boardRow->addWidget(ordersPanel, 3);

    QFrame* alertPanel = nullptr;
    QVBoxLayout* alertColumn = buildPanel(QStringLiteral("Alerts & Today's Book"),
                                          QStringLiteral("Low stock, expiring goods and today's reservations"),
                                          body, &alertPanel);
    m_alerts = new QListWidget(alertPanel);
    m_alerts->setObjectName(QStringLiteral("dashboardAlerts"));
    m_alerts->setFrameShape(QFrame::NoFrame);
    m_alerts->setSelectionMode(QAbstractItemView::NoSelection);
    m_alerts->setFocusPolicy(Qt::NoFocus);
    m_alerts->setMinimumHeight(kListMinHeight);
    alertColumn->addWidget(m_alerts, 1);
    boardRow->addWidget(alertPanel, 2);

    bodyColumn->addLayout(boardRow);

    bodyColumn->addStretch(1);
    scroll->setWidget(body);
    root->addWidget(scroll, 1);

    // A theme switch changes every colour the chart and the list bullets were painted with,
    // so the whole screen is simply rebuilt from the new palette.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            &DashboardPage::refresh);

    // Deliberately NOT calling refresh() here. Page's contract is that MainWindow refreshes a
    // screen on first show, and MainWindow's constructor ends with onNavigate(kDashboard) — which
    // does exactly that. Refreshing from the constructor as well ran every dashboard query twice
    // on start-up and restarted the entrance animation mid-flight, one refresh over the top of
    // the other. The page is built empty here and populated once, by its owner.
}

QString DashboardPage::pageTitle() const { return QStringLiteral("Dashboard"); }

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void DashboardPage::refresh() {
    const Palette& pal = ThemeManager::instance().palette();

    // ONE date for the whole screen, and it is the local one.
    //
    // services::ReportService's windowing contract is explicit: every QDate crossing it is a
    // **local** calendar date, because a business day opens at local midnight — and it says in as
    // many words that callers pass QDate::currentDate() and never
    // QDateTime::currentDateTimeUtc().date(). This screen used to pass the UTC date, which in Nepal
    // (UTC+05:45) is a *different date* for the first quarter of every day: the headline "Today's
    // Sales", the seven-day chart and the "dddd, d MMMM yyyy" printed under the page title could
    // each be describing a different day, and the tile could disagree with the Reports screen about
    // the very same date. Reservations are a wall-clock booking calendar and were already local, so
    // there is now a single `today` behind every figure here — no second date to drift against.
    const QDate today = QDate::currentDate();

    try {
        // ---- headline metrics --------------------------------------------
        const core::Money todaySales = m_ctx.reports().salesForDay(today);
        const core::Money yesterdaySales = m_ctx.reports().salesForDay(today.addDays(-1));
        const std::array<core::Money, 7> week = m_ctx.reports().weeklySales(today);
        const core::Money monthSales =
            m_ctx.reports().salesForMonth(today.year(), today.month());
        const int pending = m_ctx.reports().pendingOrderCount();
        const int customers = m_ctx.reports().customerCount();
        const std::vector<models::Ingredient> low = m_ctx.inventory().lowStock();
        const std::vector<models::Ingredient> expiring = m_ctx.inventory().expiring(7);

        // The headline number and its subtitle are the same figure from the same query, named
        // once so they cannot drift apart ("2" over "3 need reordering").
        const int lowCount = static_cast<int>(low.size());

        core::Money weekTotal;
        for (const core::Money& day : week) weekTotal += day;

        countUp(m_cards[0], static_cast<double>(todaySales.paisa()), formatPaisa, 0);
        countUp(m_cards[1], static_cast<double>(pending), formatCount, 80);
        countUp(m_cards[2], static_cast<double>(customers), formatCount, 160);
        countUp(m_cards[3], static_cast<double>(lowCount), formatCount, 240);

        // Tones, not just up/down: "No sales recorded yet today" and "1 in the pipeline" state a
        // fact and are muted; red is kept for the one line here that reports a real problem.
        if (yesterdaySales.isZero()) {
            setTrendLine(m_cards[0],
                         todaySales.isZero() ? QStringLiteral("No sales recorded yet today")
                                             : QStringLiteral("First takings of the day"),
                         todaySales.isZero() ? Tone::Neutral : Tone::Positive);
        } else {
            const long long delta = todaySales.paisa() - yesterdaySales.paisa();
            const long long pct = (delta * 100) / yesterdaySales.paisa();
            setTrendLine(m_cards[0],
                         QStringLiteral("%1%2% vs yesterday")
                             .arg(pct >= 0 ? QStringLiteral("+") : QString())
                             .arg(pct),
                         pct >= 0 ? Tone::Positive : Tone::Negative);
        }
        setTrendLine(m_cards[1],
                     pending == 0 ? QStringLiteral("Kitchen is clear")
                                  : QStringLiteral("%1 in the pipeline").arg(pending),
                     pending == 0 ? Tone::Positive : Tone::Neutral);
        setTrendLine(m_cards[2], QStringLiteral("Registered loyalty guests"), Tone::Neutral);
        setTrendLine(m_cards[3],
                     lowCount == 0 ? QStringLiteral("Store room is healthy")
                                   : QStringLiteral("%1 need reordering").arg(lowCount),
                     lowCount == 0 ? Tone::Positive : Tone::Negative);

        // ---- seven-day revenue chart -------------------------------------
        auto* bars = new QBarSet(QStringLiteral("Revenue"));
        bars->setColor(pal.primary);
        bars->setBorderColor(pal.primary);
        bars->setLabelColor(pal.text);

        QStringList dayLabels;
        double maxRupees = 0.0;
        for (int i = 0; i < 7; ++i) {
            const QDate day = today.addDays(i - 6);
            const double rupees = static_cast<double>(week[static_cast<std::size_t>(i)].paisa()) / 100.0;
            *bars << rupees;
            maxRupees = std::max(maxRupees, rupees);
            dayLabels << (i == 6 ? QStringLiteral("Today")
                                 : day.toString(QStringLiteral("ddd")));
        }

        auto* series = new QBarSeries();
        series->append(bars);
        series->setBarWidth(0.55);

        // Surface, axes and — the point of the exercise — the *tick algorithm* all come from
        // gui::chartkit, the one place that decides what an AluChop chart looks like. This screen
        // used to scale its own axis straight off the data and print ticks like
        // 0 · 2677 · 5354 · 8031 · 10709 while the Reports chart, two clicks away, rounded to
        // 0 · 5000 · 10000 · 15000. Two tick algorithms in one product is exactly what makes
        // software look unfinished, so there is now only one, and the empty-data fallback is
        // shared with it too.
        QChart* chart = chartkit::newChart(QMargins(0, 4, 4, 0), 450);
        chart->addSeries(series);

        QBarCategoryAxis* axisX = chartkit::newCategoryAxis(dayLabels, pal);
        QValueAxis* axisY = chartkit::newValueAxis(0.0, maxRupees, pal);

        chart->addAxis(axisX, Qt::AlignBottom);
        chart->addAxis(axisY, Qt::AlignLeft);
        series->attachAxis(axisX);
        series->attachAxis(axisY);

        m_revenueChart->setChart(chart);  // the view takes ownership and drops the old chart
        m_revenueChart->setToolTip(QStringLiteral("This week %1  ·  This month %2")
                                       .arg(core::formatNpr(weekTotal),
                                            core::formatNpr(monthSales)));

        // Nothing settled all week means there is no plot to draw — show the empty state in the
        // chart's place rather than a bare, meaningless grid.
        const bool hasRevenue = maxRupees > 0.0;
        m_revenueChart->setVisible(hasRevenue);
        if (auto* chartEmpty = m_revenueChart->parentWidget()->findChild<QFrame*>(
                QStringLiteral("emptyState"), Qt::FindDirectChildrenOnly)) {
            chartEmpty->setVisible(!hasRevenue);
        }

        // The chart panel's only direct QLabel child carrying the "mutedLabel" name is its
        // subtitle line, so the weekly/monthly totals can be surfaced there without the page
        // having to keep another member pointer.
        if (auto* subtitle = m_revenueChart->parentWidget()->findChild<QLabel*>(
                QStringLiteral("mutedLabel"), Qt::FindDirectChildrenOnly)) {
            subtitle->setText(QStringLiteral("This week %1  ·  This month %2")
                                  .arg(core::formatNpr(weekTotal), core::formatNpr(monthSales)));
        }

        // ---- popular items ------------------------------------------------
        m_popularItems->clear();
        const std::vector<std::pair<QString, int>> popular = m_ctx.reports().popularItems(5);
        if (popular.empty()) {
            addEmptyRow(m_popularItems,
                        QStringLiteral("No dishes sold yet — the first order will show up here."),
                        pal.textMuted);
        } else {
            int rank = 1;
            for (const auto& entry : popular) {
                addRichRow(m_popularItems, bulletIcon(pal.accent),
                           QStringLiteral("%1.  %2").arg(rank).arg(entry.first),
                           QStringLiteral("%1 sold").arg(entry.second), pal.primary);
                ++rank;
            }
        }

        // ---- alerts + today's reservations --------------------------------
        m_alerts->clear();
        int alertCount = 0;
        for (const models::Ingredient& ing : low) {
            addRichRow(m_alerts, bulletIcon(pal.danger),
                       QStringLiteral("Low stock — %1").arg(ing.name()),
                       QStringLiteral("%1 %2").arg(ing.stockQty(), 0, 'f', 2).arg(ing.unit()),
                       pal.danger);
            ++alertCount;
        }
        for (const models::Ingredient& ing : expiring) {
            if (ing.isLow()) continue;  // already listed above
            addRichRow(m_alerts, bulletIcon(pal.secondary),
                       QStringLiteral("Expiring — %1").arg(ing.name()),
                       ing.expiryDate().toString(QStringLiteral("d MMM")), pal.secondary);
            ++alertCount;
        }
        const std::vector<models::Reservation> bookings = m_ctx.reservations().onDay(today);
        for (const models::Reservation& r : bookings) {
            if (r.status() == models::ReservationStatus::Cancelled ||
                r.status() == models::ReservationStatus::NoShow)
                continue;
            addRichRow(m_alerts, bulletIcon(pal.primary),
                       QStringLiteral("%1 — %2 guest%3")
                           .arg(r.customerName())
                           .arg(r.guests())
                           .arg(r.guests() == 1 ? QString() : QStringLiteral("s")),
                       r.startsAt().toLocalTime().toString(QStringLiteral("hh:mm")), pal.primary);
            ++alertCount;
        }
        if (alertCount == 0) {
            addEmptyRow(m_alerts,
                        QStringLiteral("All clear — stock is healthy and nothing is booked today."),
                        pal.textMuted);
        }

        // ---- live orders ---------------------------------------------------
        std::vector<models::Order> active = m_ctx.orders().activeOrders();
        std::sort(active.begin(), active.end(),
                  [](const models::Order& a, const models::Order& b) {
                      return a.createdAt() > b.createdAt();
                  });

        QHash<int, QString> tableNames;
        for (const models::Table& t : m_ctx.reservations().tables())
            tableNames.insert(t.id(), t.name());

        m_pendingOrders->clearSpans();  // the empty-state row spans every column
        m_pendingOrders->setRowCount(0);
        int row = 0;
        for (const models::Order& order : active) {
            m_pendingOrders->insertRow(row);
            m_pendingOrders->setItem(row, 0, new QTableWidgetItem(order.orderNumber()));
            m_pendingOrders->setItem(row, 1, new QTableWidgetItem(typeText(order.type())));
            m_pendingOrders->setItem(
                row, 2,
                new QTableWidgetItem(order.tableId() > 0
                                         ? tableNames.value(order.tableId(),
                                                            QStringLiteral("#%1").arg(order.tableId()))
                                         : QStringLiteral("—")));

            auto* statusCell = new QTableWidgetItem(statusText(order.status()));
            statusCell->setForeground(statusColour(order.status(), pal));
            QFont sf = statusCell->font();
            sf.setWeight(QFont::DemiBold);
            statusCell->setFont(sf);
            m_pendingOrders->setItem(row, 3, statusCell);

            m_pendingOrders->setItem(row, 4, moneyCell(order.subtotal()));
            ++row;
        }
        if (row == 0) {
            m_pendingOrders->insertRow(0);
            auto* empty = new QTableWidgetItem(
                QStringLiteral("No orders on the floor — open one from the Orders screen (Ctrl+N)."));
            empty->setTextAlignment(Qt::AlignCenter);
            empty->setForeground(pal.textMuted);
            empty->setFlags(Qt::NoItemFlags);
            m_pendingOrders->setItem(0, 0, empty);
            m_pendingOrders->setSpan(0, 0, 1, m_pendingOrders->columnCount());
            m_pendingOrders->setRowHeight(0, 88);
        }

        // ---- give every panel room for whole rows --------------------------
        // Each list is sized to its *own* whole rows. They used to share one height, which was
        // right when they sat side by side and had to read as a pair; now that they head two
        // different bands, forcing the three-item best-seller list to the height of a five-item
        // alert feed only bought 150 px of bare card under three rows — empty space beside a
        // cramped neighbour, which is the exact fault this pass is here to remove.
        {
            const int listChrome = 2 * m_popularItems->frameWidth();
            pinHeight(m_popularItems,
                      std::max(kListMinHeight,
                               wholeRowsHeight(listRowHeights(m_popularItems), listChrome,
                                               kListMaxHeight)));
            pinHeight(m_alerts,
                      std::max(kListMinHeight,
                               wholeRowsHeight(listRowHeights(m_alerts), listChrome,
                                               kListMaxHeight)));

            // The order board is *floored* at whole rows rather than pinned to them: it now shares
            // a band with the alert list, and a maximum here would leave a strip of bare card under
            // the table whenever the list beside it happens to be the taller of the two.
            std::vector<int> orderRows;
            orderRows.reserve(static_cast<std::size_t>(m_pendingOrders->rowCount()));
            for (int r = 0; r < m_pendingOrders->rowCount(); ++r)
                orderRows.push_back(m_pendingOrders->rowHeight(r));
            const int tableChrome = 2 * m_pendingOrders->frameWidth() +
                                    m_pendingOrders->horizontalHeader()->sizeHint().height();
            m_pendingOrders->setMinimumHeight(
                std::max(kTableMinHeight,
                         wholeRowsHeight(orderRows, tableChrome, kListMaxHeight)));
        }

        animateCards();
    } catch (const std::exception& e) {
        // refresh() must never throw (Page contract) — a broken read becomes a visible notice.
        m_ctx.notifications().notify(QStringLiteral("Dashboard"),
                                     QString::fromUtf8(e.what()), 3);
    }
}

// ---------------------------------------------------------------------------
// Entrance animation
// ---------------------------------------------------------------------------

void DashboardPage::animateCards() {
    /// @oop-concept Object Arrays :: the four headline metrics are one array, so the stagger is
    /// a loop over indices rather than four hand-written calls
    for (std::size_t i = 0; i < m_cards.size(); ++i) {
        if (m_cards[i]) m_cards[i]->animateIn(static_cast<int>(i) * 80);
    }
}

} // namespace aluchop::gui

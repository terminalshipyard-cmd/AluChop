/**
 * @file OrdersPage.cpp
 * @brief Screen 3 — the point of sale: menu picker, order ticket, kitchen board, split/merge, billing.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * This is the busiest screen in the application and the one a service actually runs on:
 * pick a dish on the left, watch it land on the ticket on the right, fire it at the kitchen,
 * advance it down the pass and settle it. Every one of those steps is a services::OrderService
 * call; line edits additionally travel through services::CommandStack so a mis-tapped dish is
 * one Ctrl+Z away.
 *
 * @warning Prices are tax-inclusive everywhere on this screen. The running total is the sum of
 *          the line totals; nothing is added on top.
 */

#include "aluchop/gui/OrdersPage.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMargins>
#include <QMessageBox>
#include <QModelIndex>
#include <QModelIndexList>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRect>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <vector>

#include "aluchop/core/Money.hpp"
#include "aluchop/gui/BillingDialog.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/gui/Widgets.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/MenuItem.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/models/OrderItem.hpp"
#include "aluchop/models/Table.hpp"
#include "aluchop/services/AppContext.hpp"

namespace aluchop::gui {
namespace {

constexpr int kIdRole = Qt::UserRole;            ///< order id / menu-item id carried by a row
constexpr int kNameRole = Qt::UserRole + 1;      ///< dish name, drawn by PickerRowDelegate
constexpr int kCategoryRole = Qt::UserRole + 2;  ///< dish section, drawn under the name
constexpr int kPriceRole = Qt::UserRole + 3;     ///< pre-formatted, tax-inclusive NPR price

// Kitchen-pass geometry — the numbers fitKitchenPanel() needs to turn a row count into a height.
constexpr int kRowMargin = 2;        ///< QSS `QListWidget::item { margin: 1px 0px }`, both edges
constexpr int kListPadding = 12;     ///< QSS `QListWidget { padding: 5px }` plus a hair of slack
constexpr int kPanelChrome = 90;     ///< card margins + heading + subtitle + the spacings between
constexpr int kMinPassHeight = 148;  ///< below this the card stops reading as a deliberate panel

// ---------------------------------------------------------------------------
// Presentation helpers
// ---------------------------------------------------------------------------

/**
 * @brief Tags a widget so it can be found again later.
 *
 * Deliberately a *dynamic property* rather than an objectName: objectName is the QSS selector
 * (ThemeManager's contract lists `searchBar`, `ghostButton`, `mutedLabel`…), so renaming a widget
 * to make it findable would silently strip its styling.
 */
void tagWidget(QWidget* w, const QString& name) { w->setProperty("aluchopTag", name); }

/// @oop-concept Function Template :: one lookup serves every widget type this screen tags
template <typename T>
T findTagged(const QWidget* root, const QString& name) {
    const auto candidates = root->template findChildren<T>();
    for (T w : candidates)
        if (w->property("aluchopTag").toString() == name) return w;
    return nullptr;
}

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
    b->setMinimumHeight(36);
    return b;
}

void tint(QLabel* label, const QColor& colour) {
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, colour);
    label->setPalette(p);
}

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

QString typeText(models::OrderType t) {
    switch (t) {
        case models::OrderType::DineIn:   return QStringLiteral("Dine-In");
        case models::OrderType::Takeaway: return QStringLiteral("Takeaway");
        case models::OrderType::Delivery: return QStringLiteral("Delivery");
    }
    return QString();
}

/// The next rung of the kitchen ladder, phrased as the button a waiter would press.
QString nextStepText(models::OrderStatus s) {
    switch (s) {
        case models::OrderStatus::Pending:   return QStringLiteral("Start cooking");
        case models::OrderStatus::Preparing: return QStringLiteral("Mark ready");
        case models::OrderStatus::Ready:     return QStringLiteral("Mark served");
        default:                             return QStringLiteral("Advance status");
    }
}

/// Builds a card surface with a heading and returns the body layout to fill.
QVBoxLayout* buildPanel(const QString& title, QWidget* parent, QFrame** outPanel,
                        const QString& subtitle = QString()) {
    auto* panel = new GlassPanel(parent);
    panel->setObjectName(QStringLiteral("card"));
    auto* column = new QVBoxLayout(panel);
    column->setContentsMargins(16, 14, 16, 14);
    column->setSpacing(10);
    column->addWidget(styledLabel(title, QStringLiteral("sectionTitle"), panel, 1, QFont::DemiBold));
    if (!subtitle.isEmpty())
        column->addWidget(styledLabel(subtitle, QStringLiteral("mutedLabel"), panel, -1));
    *outPanel = panel;
    return column;
}

/**
 * @brief Draws one two-line row: the identifying name on top, a quiet caption under it.
 *
 * Used twice on this screen — once for a dish in the POS picker (name over its section, with the
 * tax-inclusive price parked at the right end of the *caption* line) and once for a line on the
 * current ticket (dish over "2 × Rs 450.00"). Both rows are the same shape, so they are the same
 * delegate.
 *
 * @par Why the price sits on the second line
 * It used to share line one with the name, taking ~90 px off the name's budget. In a POS pane
 * that is the difference between "Barahsinghe Lager (650ml)" and "Barahsinghe La…", and a dish
 * you cannot read is a dish you cannot sell. The name now owns the full row width; the price
 * keeps its right-hand anchor one line down, where the only thing it competes with is a short
 * section caption.
 *
 * @par Why a delegate and not a row of QLabels
 * The previous implementation put a small widget in every row with QListWidget::setItemWidget.
 * That looks harmless and is not: the generated stylesheet gives `QListWidget::item` a
 * `padding: 9px 12px`, and Qt hands an item widget only the *content* rectangle — 18 px shorter
 * and 24 px narrower than the row. Two stacked labels were therefore squeezed into less height
 * than they needed and the dish name printed straight through the section beneath it, on every
 * single row. Painting the row instead removes the dependency on someone else's padding
 * entirely (this delegate owns its own) and spares the screen 126 throw-away widget trees on
 * every keystroke in the search box.
 *
 * @oop-concept Runtime Polymorphism :: QListView calls paint()/sizeHint() through the abstract
 *              QAbstractItemDelegate interface; the view has no idea what a dish looks like
 * @oop-concept Method Overriding :: rows carrying no dish (the "no dishes match" placeholder,
 *              and every cell of the ticket's amount column) fall through to the inherited
 *              QStyledItemDelegate behaviour untouched
 */
class PickerRowDelegate : public QStyledItemDelegate {
public:
    /// @param parent owning view (Qt parent-owned; a delegate is never owned by the view itself).
    explicit PickerRowDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    /// @return a two-line row, tall enough that descenders never touch the line below.
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.data(kNameRole).isValid())
            return QStyledItemDelegate::sizeHint(option, index);
        const QFontMetrics nameFm(nameFont(option.font));
        return QSize(0, 2 * kPadV + nameFm.height() + kLineGap + captionHeight(option.font));
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        if (!index.data(kNameRole).isValid()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        // Let the style paint the row's own background so hover, selection and the rounded
        // corners keep coming from ThemeManager rather than from a hard-coded colour here.
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.icon = QIcon();
        opt.features &= ~QStyleOptionViewItem::HasDisplay;
        QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const Palette& pal = ThemeManager::instance().palette();
        const QFont nameF = nameFont(option.font);
        const QFont sectionF = sectionFont(option.font);
        const QFont priceF = priceFont(option.font);
        const QFontMetrics nameFm(nameF);
        const QFontMetrics sectionFm(sectionF);
        const QFontMetrics priceFm(priceF);

        const QString priceText = index.data(kPriceRole).toString();
        const int priceWidth =
            priceText.isEmpty() ? 0 : priceFm.horizontalAdvance(priceText) + kPriceGap;

        const QRect body = option.rect.adjusted(kPadH, kPadV, -kPadH, -kPadV);
        const int caption = captionHeight(option.font);

        // Centre the two-line block in whatever height the row actually got.
        const int block = nameFm.height() + kLineGap + caption;
        const int top = body.top() + std::max(0, (body.height() - block) / 2);
        const int secondTop = top + nameFm.height() + kLineGap;

        painter->save();

        // Line one belongs to the identifier alone — it is never shortened to make room for
        // anything else on the row.
        painter->setFont(nameF);
        painter->setPen(pal.text);
        painter->drawText(QRect(body.left(), top, body.width(), nameFm.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          nameFm.elidedText(index.data(kNameRole).toString(), Qt::ElideRight,
                                            body.width()));

        const int captionWidth = std::max(0, body.width() - priceWidth);
        painter->setFont(sectionF);
        painter->setPen(pal.textMuted);
        painter->drawText(QRect(body.left(), secondTop, captionWidth, caption),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          sectionFm.elidedText(index.data(kCategoryRole).toString(),
                                               Qt::ElideRight, captionWidth));

        if (!priceText.isEmpty()) {
            painter->setFont(priceF);
            painter->setPen(pal.primary);
            painter->drawText(QRect(body.right() - priceWidth + kPriceGap, secondTop,
                                    priceWidth - kPriceGap + 1, caption),
                              Qt::AlignRight | Qt::AlignVCenter, priceText);
        }
        painter->restore();
    }

private:
    static constexpr int kPadH = 12;      ///< own horizontal padding, independent of any QSS
    static constexpr int kPadV = 11;      ///< own vertical padding — the row's breathing room
    static constexpr int kLineGap = 3;    ///< never 0: the caption must clear the name's descenders
    static constexpr int kPriceGap = 14;  ///< gutter between an elided caption and the price

    /// @oop-concept Static Members :: pure font derivations, no per-delegate state to carry
    static QFont nameFont(const QFont& base) {
        QFont f = base;
        f.setWeight(QFont::DemiBold);
        return f;
    }
    static QFont sectionFont(const QFont& base) {
        QFont f = base;
        f.setPointSize(std::max(8, base.pointSize() - 1));
        return f;
    }
    /// The price shares the caption line, so it shares the caption size — only the weight and
    /// the colour mark it out.
    static QFont priceFont(const QFont& base) {
        QFont f = sectionFont(base);
        f.setWeight(QFont::DemiBold);
        return f;
    }
    /// Tallest of the two things that can appear on line two.
    static int captionHeight(const QFont& base) {
        return std::max(QFontMetrics(sectionFont(base)).height(),
                        QFontMetrics(priceFont(base)).height());
    }
};

/**
 * @brief A list whose viewport is always a whole number of rows tall.
 *
 * A scrolling panel that ends in a row sliced through the middle looks broken rather than
 * scrollable. This list keeps the leftover pixels as a bottom margin instead of as half a dish,
 * so the picker always ends on a clean row edge however the splitter is dragged.
 *
 * @oop-concept Single Inheritance :: behaviour is inherited wholesale; only the two hooks that
 *              know when the geometry changed (resizeEvent, rowsInserted) are overridden
 */
class WholeRowListWidget : public QListWidget {
public:
    /// @param parent owning widget.
    explicit WholeRowListWidget(QWidget* parent = nullptr) : QListWidget(parent) {}

protected:
    void resizeEvent(QResizeEvent* event) override {
        QListWidget::resizeEvent(event);
        snapToWholeRows();
    }

    void rowsInserted(const QModelIndex& parent, int start, int end) override {
        QListView::rowsInserted(parent, start, end);
        snapToWholeRows();
    }

private:
    /// Parks the remainder of the division in the bottom margin. Idempotent: recomputing after
    /// the viewport has shrunk yields the same answer and returns without touching anything.
    void snapToWholeRows() {
        const QMargins margins = viewportMargins();
        const int step = count() > 0 ? sizeHintForRow(0) : 0;
        const int wanted = step > 0 ? (viewport()->height() + margins.bottom()) % step : 0;
        if (wanted == margins.bottom()) return;
        setViewportMargins(margins.left(), margins.top(), margins.right(), wanted);
    }
};

/**
 * @brief A table whose viewport is always a whole number of rows tall.
 *
 * A scrolling list that ends in a row sliced through the middle reads as broken rather than as
 * scrollable — and the floor list is the one place on this screen where a half-drawn order number
 * would be mistaken for a real one. The leftover pixels become a bottom margin instead.
 *
 * @oop-concept Multilevel Inheritance :: QTableWidget → ElegantTable (the house table style) →
 *              this screen's whole-row refinement; each level adds exactly one idea
 */
class WholeRowTable : public ElegantTable {
public:
    /// @param headers column captions.
    /// @param parent owning widget.
    explicit WholeRowTable(const QStringList& headers, QWidget* parent = nullptr)
        : ElegantTable(headers, parent) {}

protected:
    void resizeEvent(QResizeEvent* event) override {
        ElegantTable::resizeEvent(event);
        snapToWholeRows();
    }

    void rowsInserted(const QModelIndex& parent, int start, int end) override {
        QTableView::rowsInserted(parent, start, end);
        snapToWholeRows();
    }

private:
    /// Parks the remainder of the division in the bottom margin.
    ///
    /// @note The left/top/right margins are read back and preserved, never assumed: QTableView
    ///       keeps its own header sizes there, and overwriting them would push the column header
    ///       on top of the first row. Only the bottom is ours.
    void snapToWholeRows() {
        const QMargins margins = viewportMargins();
        const int step = verticalHeader()->defaultSectionSize();
        const int wanted =
            (step > 0 && rowCount() > 0) ? (viewport()->height() + margins.bottom()) % step : 0;
        if (wanted == margins.bottom()) return;
        setViewportMargins(margins.left(), margins.top(), margins.right(), wanted);
    }
};

/// Centred "nothing here" row for a list widget.
void addEmptyRow(QListWidget* list, const QString& text, const QColor& colour) {
    auto* item = new QListWidgetItem(text, list);
    item->setFlags(Qt::NoItemFlags);
    item->setTextAlignment(Qt::AlignCenter);
    item->setForeground(colour);
    item->setSizeHint(QSize(0, 60));
}

/**
 * @brief Gives the Kitchen Pass exactly the height its tickets need, and the rest to the floor.
 *
 * A splitter divides whatever it is given, so a pass holding one line and a floor list holding
 * fifteen orders used to be handed the same two fixed slabs: a half-empty kitchen card with a
 * scrollbar on content that fits, next to an orders list that had run out of room. Re-seeding the
 * split from the board's real content height after every refresh fixes both ends at once, and the
 * handle still drags because these are only sizes, not constraints.
 *
 * @param page the OrdersPage (the splitter is found by object name — no new member is needed).
 * @param board the freshly repopulated kitchen list.
 */
void fitKitchenPanel(const QWidget* page, QListWidget* board) {
    auto* leftSplitter = page->findChild<QSplitter*>(QStringLiteral("posLeftSplitter"));
    if (!leftSplitter || leftSplitter->count() < 2 || !board) return;

    int content = 0;
    for (int row = 0; row < board->count(); ++row) {
        const QListWidgetItem* item = board->item(row);
        content += (item ? item->sizeHint().height() : 0) + kRowMargin;
    }
    content += kListPadding;

    const QList<int> current = leftSplitter->sizes();
    // The splitter's own height is authoritative once it has been laid out; during construction
    // it is still zero, and the seeded sizes are the only truth there is.
    const int laidOut = leftSplitter->height() - leftSplitter->handleWidth();
    const int total = laidOut > kMinPassHeight * 2 ? laidOut : current.at(0) + current.at(1);

    // Never smaller than a card that reads as deliberate, never more than two fifths of the
    // column — the floor list is the one that has to keep growing.
    const int wanted = std::max(kMinPassHeight,
                                std::min(content + kPanelChrome, std::max(1, total * 2 / 5)));

    // A ceiling, not a suggestion. setSizes() alone is rescaled by QSplitter whenever the column
    // is a different height than it was when the sizes were computed, which is exactly the case
    // on the very first layout — and a pass that grows past its own content is the half-empty
    // panel this is here to prevent.
    if (QWidget* panel = leftSplitter->widget(1)) panel->setMaximumHeight(wanted);
    if (wanted == current.at(1)) return;
    leftSplitter->setSizes(QList<int>{std::max(1, total - wanted), wanted});
}

/// Re-runs the menu query behind the POS picker.
void populatePicker(services::AppContext& ctx, QWidget* page) {
    const Palette& pal = ThemeManager::instance().palette();
    auto* list = page->findChild<QListWidget*>(QStringLiteral("posItems"));
    auto* category = page->findChild<QComboBox*>(QStringLiteral("posCategory"));
    auto* search = findTagged<QLineEdit*>(page, QStringLiteral("posSearch"));
    if (!list) return;

    std::vector<models::MenuItem> items;
    try {
        items = ctx.menu().search(search ? search->text().trimmed() : QString(),
                                  category ? category->currentData().toString() : QString(),
                                  /*availableOnly=*/true, services::MenuSort::NameAsc);
    } catch (const std::exception& e) {
        ctx.notifications().notify(QStringLiteral("Menu"), QString::fromUtf8(e.what()), 3);
        return;
    }

    list->clear();
    if (items.empty()) {
        addEmptyRow(list, QStringLiteral("No dishes match that search."), pal.textMuted);
        return;
    }
    for (const models::MenuItem& item : items) {
        // The row carries data, not widgets: PickerRowDelegate paints the name, its section and
        // the price, sizes the row from the real font metrics and elides instead of ever asking
        // the list to scroll sideways.
        auto* entry = new QListWidgetItem(list);
        entry->setData(kIdRole, item.id());
        entry->setData(kNameRole, item.name());
        entry->setData(kCategoryRole, item.category());
        entry->setData(kPriceRole, core::formatNpr(item.price()));
        entry->setToolTip(item.description().isEmpty()
                              ? QStringLiteral("%1  ·  %2").arg(item.name(),
                                                                core::formatNpr(item.price()))
                              : QStringLiteral("%1  ·  %2\n%3")
                                    .arg(item.name(), core::formatNpr(item.price()),
                                         item.description()));
    }
}

/// Repaints the ticket pane (lines, header line and running total) for one order.
void fillTicket(services::AppContext& ctx, QWidget* page, QTableWidget* itemsView, int orderId) {
    const Palette& pal = ThemeManager::instance().palette();
    auto* header = findTagged<QLabel*>(page, QStringLiteral("ticketHeader"));
    auto* totalLabel = findTagged<QLabel*>(page, QStringLiteral("ticketTotal"));
    auto* noteLabel = findTagged<QLabel*>(page, QStringLiteral("ticketNote"));
    auto* advanceBtn = findTagged<QPushButton*>(page, QStringLiteral("advanceButton"));

    itemsView->clearSpans();  // the empty-state row spans every column; drop it before refilling
    itemsView->setRowCount(0);

    std::optional<models::Order> order;
    if (orderId > 0) {
        try {
            order = ctx.orders().order(orderId);
        } catch (const std::exception& e) {
            ctx.notifications().notify(QStringLiteral("Orders"), QString::fromUtf8(e.what()), 3);
        }
    }

    if (!order.has_value()) {
        if (header) {
            header->setText(QStringLiteral("No order selected — start one with “New order”."));
            tint(header, pal.textMuted);
        }
        if (noteLabel) noteLabel->setVisible(false);
        if (totalLabel) totalLabel->setText(core::formatNpr(core::Money::zero()));
        if (advanceBtn) advanceBtn->setText(QStringLiteral("Advance status"));
        return;
    }

    QString tableName;
    if (order->tableId() > 0) {
        for (const models::Table& t : ctx.reservations().tables()) {
            if (t.id() == order->tableId()) {
                tableName = t.name();
                break;
            }
        }
        if (tableName.isEmpty()) tableName = QStringLiteral("#%1").arg(order->tableId());
    }

    if (header) {
        QString text = QStringLiteral("%1  ·  %2").arg(order->orderNumber(), typeText(order->type()));
        if (!tableName.isEmpty()) text += QStringLiteral("  ·  Table %1").arg(tableName);
        text += QStringLiteral("  ·  %1").arg(statusText(order->status()));
        header->setText(text);
        tint(header, statusColour(order->status(), pal));
    }
    if (noteLabel) {
        noteLabel->setText(QStringLiteral("Note: %1").arg(order->note()));
        noteLabel->setVisible(!order->note().isEmpty());
    }
    if (advanceBtn) advanceBtn->setText(nextStepText(order->status()));

    /// @oop-concept Subscript Operator (surfaced) :: the ticket reads the order as the sequence of
    /// lines it is — `order->items()` hands the vector over without a copy
    int row = 0;
    for (const models::OrderItem& line : order->items()) {
        itemsView->insertRow(row);

        // The dish column is painted by PickerRowDelegate: the name owns the full width of the
        // column and the quantity/unit price ride underneath it as a caption. That is what keeps
        // "Barahsinghe Lager (650ml)" readable in a ticket pane that used to show it as
        // "Barahsinghe La…" — nothing on the ticket competes with the dish for line one.
        QString caption = QStringLiteral("%1 × %2")
                              .arg(line.qty())
                              .arg(core::formatNpr(line.unitPrice()));
        if (!line.note().isEmpty()) caption += QStringLiteral("  ·  %1").arg(line.note());

        auto* nameCell = new QTableWidgetItem(line.name());
        nameCell->setData(kNameRole, line.name());
        nameCell->setData(kCategoryRole, caption);
        nameCell->setToolTip(QStringLiteral("%1\n%2").arg(line.name(), caption));
        itemsView->setItem(row, 0, nameCell);

        auto* lineCell = new QTableWidgetItem(core::formatNpr(line.lineTotal()));
        QFont lf = lineCell->font();
        lf.setWeight(QFont::DemiBold);
        lineCell->setFont(lf);
        lineCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        itemsView->setItem(row, 1, lineCell);

        ++row;
    }

    if (row == 0) {
        itemsView->insertRow(0);
        auto* empty = new QTableWidgetItem(
            QStringLiteral("Nothing on this ticket yet — pick a dish from the menu."));
        empty->setTextAlignment(Qt::AlignCenter);
        empty->setForeground(pal.textMuted);
        empty->setFlags(Qt::NoItemFlags);
        itemsView->setItem(0, 0, empty);
        itemsView->setSpan(0, 0, 1, itemsView->columnCount());
        itemsView->setRowHeight(0, 76);
    }

    if (totalLabel) totalLabel->setText(core::formatNpr(order->subtotal()));
}

/// Modal used by both "New order" and "Edit order".
struct OrderHeaderChoice {
    models::OrderType type = models::OrderType::DineIn;
    int tableId = 0;
    int customerId = 0;
    QString note;
};

/**
 * @brief Collects (or edits) an order header.
 * @param editing when true the type/table/customer are shown read-only, because
 *        services::OrderService exposes only setOrderNote for an existing order.
 * @return true when the user accepted.
 */
bool orderHeaderDialog(QWidget* parent, services::AppContext& ctx, OrderHeaderChoice& choice,
                       bool editing) {
    QDialog dlg(parent);
    dlg.setWindowTitle(editing ? QStringLiteral("Edit order") : QStringLiteral("New order"));
    dlg.setMinimumWidth(440);

    auto* column = new QVBoxLayout(&dlg);
    column->setContentsMargins(22, 20, 22, 18);
    column->setSpacing(14);
    column->addWidget(styledLabel(editing ? QStringLiteral("Order details")
                                          : QStringLiteral("Open a new order"),
                                  QStringLiteral("sectionTitle"), &dlg, 3, QFont::DemiBold));

    auto* form = new QFormLayout();
    form->setSpacing(10);

    auto* type = new QComboBox(&dlg);
    type->setMinimumHeight(36);
    type->addItem(QStringLiteral("Dine-In"), static_cast<int>(models::OrderType::DineIn));
    type->addItem(QStringLiteral("Takeaway"), static_cast<int>(models::OrderType::Takeaway));
    type->addItem(QStringLiteral("Delivery"), static_cast<int>(models::OrderType::Delivery));
    type->setCurrentIndex(type->findData(static_cast<int>(choice.type)));

    auto* table = new QComboBox(&dlg);
    table->setMinimumHeight(36);
    table->addItem(QStringLiteral("— no table —"), 0);
    try {
        for (const models::Table& t : ctx.reservations().tables()) {
            if (!t.isActive()) continue;
            table->addItem(QStringLiteral("%1  (%2 seats)").arg(t.name()).arg(t.capacity()), t.id());
        }
    } catch (const std::exception& e) {
        ctx.notifications().notify(QStringLiteral("Tables"), QString::fromUtf8(e.what()), 3);
    }
    const int tableIndex = table->findData(choice.tableId);
    table->setCurrentIndex(tableIndex >= 0 ? tableIndex : 0);

    auto* phone = new QLineEdit(&dlg);
    phone->setMinimumHeight(36);
    phone->setPlaceholderText(QStringLiteral("Loyalty phone number (optional)"));
    auto* phoneHint = styledLabel(QStringLiteral("Walk-in guest"), QStringLiteral("mutedLabel"),
                                  &dlg, -1);

    auto* note = new QPlainTextEdit(choice.note, &dlg);
    note->setMinimumHeight(70);
    note->setPlaceholderText(QStringLiteral("Kitchen note, e.g. “no chilli, birthday cake at the end”"));

    const auto syncTableEnabled = [type, table]() {
        const bool dineIn =
            static_cast<models::OrderType>(type->currentData().toInt()) == models::OrderType::DineIn;
        table->setEnabled(dineIn);
        if (!dineIn) table->setCurrentIndex(0);
    };
    QObject::connect(type, &QComboBox::currentIndexChanged, &dlg,
                     [syncTableEnabled](int) { syncTableEnabled(); });
    syncTableEnabled();

    int resolvedCustomer = choice.customerId;
    QObject::connect(phone, &QLineEdit::textChanged, &dlg,
                     [&ctx, phoneHint, &resolvedCustomer](const QString& text) {
                         const QString trimmed = text.trimmed();
                         if (trimmed.isEmpty()) {
                             resolvedCustomer = 0;
                             phoneHint->setText(QStringLiteral("Walk-in guest"));
                             return;
                         }
                         const auto found = ctx.customers().byPhone(trimmed);
                         if (found.has_value()) {
                             resolvedCustomer = found->id();
                             phoneHint->setText(QStringLiteral("Recognised — loyalty points will "
                                                               "be awarded on settlement."));
                         } else {
                             resolvedCustomer = 0;
                             phoneHint->setText(QStringLiteral("Not in the loyalty database."));
                         }
                     });

    form->addRow(QStringLiteral("Order type"), type);
    form->addRow(QStringLiteral("Table"), table);
    if (editing) {
        // Not part of the edit flow — keep them out of the dialog entirely rather than leaving
        // parented-but-unlaid-out widgets floating at the origin.
        phone->setVisible(false);
        phoneHint->setVisible(false);
    } else {
        form->addRow(QStringLiteral("Customer"), phone);
        form->addRow(QString(), phoneHint);
    }
    form->addRow(QStringLiteral("Note"), note);
    column->addLayout(form);

    if (editing) {
        type->setEnabled(false);
        table->setEnabled(false);
        column->addWidget(styledLabel(
            QStringLiteral("Type and table are fixed once an order is open — cancel and re-open "
                           "the order to change them."),
            QStringLiteral("mutedLabel"), &dlg, -1));
    }

    auto* buttons = new QDialogButtonBox(&dlg);
    auto* ok = buttons->addButton(editing ? QStringLiteral("Save") : QStringLiteral("Open order"),
                                  QDialogButtonBox::AcceptRole);
    ok->setObjectName(QStringLiteral("primaryButton"));
    ok->setCursor(Qt::PointingHandCursor);
    ok->setMinimumHeight(36);
    auto* cancel = buttons->addButton(QStringLiteral("Cancel"), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("ghostButton"));
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setMinimumHeight(36);
    column->addWidget(buttons);

    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return false;

    choice.type = static_cast<models::OrderType>(type->currentData().toInt());
    choice.tableId = table->isEnabled() ? table->currentData().toInt() : 0;
    choice.customerId = resolvedCustomer;
    choice.note = note->toPlainText().trimmed();
    return true;
}

/// Lets the user pick one of the other open orders (used by the merge action).
int pickOtherOrderDialog(QWidget* parent, const std::vector<models::Order>& candidates,
                         const QString& title, const QString& hint) {
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(420);

    auto* column = new QVBoxLayout(&dlg);
    column->setContentsMargins(22, 20, 22, 18);
    column->setSpacing(12);
    column->addWidget(styledLabel(title, QStringLiteral("sectionTitle"), &dlg, 3, QFont::DemiBold));
    column->addWidget(styledLabel(hint, QStringLiteral("mutedLabel"), &dlg, -1));

    auto* list = new QListWidget(&dlg);
    list->setFrameShape(QFrame::NoFrame);
    list->setMinimumHeight(200);
    for (const models::Order& order : candidates) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  ·  %2  ·  %3")
                .arg(order.orderNumber(), typeText(order.type()),
                     core::formatNpr(order.subtotal())),
            list);
        item->setData(kIdRole, order.id());
    }
    if (list->count() > 0) list->setCurrentRow(0);
    column->addWidget(list, 1);

    auto* buttons = new QDialogButtonBox(&dlg);
    auto* ok = buttons->addButton(QStringLiteral("Confirm"), QDialogButtonBox::AcceptRole);
    ok->setObjectName(QStringLiteral("primaryButton"));
    ok->setCursor(Qt::PointingHandCursor);
    ok->setMinimumHeight(36);
    auto* cancel = buttons->addButton(QStringLiteral("Cancel"), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("ghostButton"));
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setMinimumHeight(36);
    column->addWidget(buttons);

    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted || !list->currentItem()) return 0;
    return list->currentItem()->data(kIdRole).toInt();
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

OrdersPage::OrdersPage(services::AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    setObjectName(QStringLiteral("ordersPage"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(16);

    // --- header -------------------------------------------------------------
    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(10);
    auto* headings = new QVBoxLayout();
    headings->setSpacing(2);
    headings->addWidget(styledLabel(pageTitle(), QStringLiteral("pageTitle"), this, 10,
                                    QFont::DemiBold));
    headings->addWidget(styledLabel(
        QStringLiteral("Pick, fire, serve, settle — every price already includes tax."),
        QStringLiteral("mutedLabel"), this));
    headerRow->addLayout(headings, 1);

    auto* splitBtn = makeButton(QStringLiteral("Split bill"), QStringLiteral("ghostButton"), this);
    connect(splitBtn, &QPushButton::clicked, this, &OrdersPage::onSplit);
    auto* mergeBtn = makeButton(QStringLiteral("Merge bills"), QStringLiteral("ghostButton"), this);
    connect(mergeBtn, &QPushButton::clicked, this, &OrdersPage::onMerge);
    auto* editBtn = makeButton(QStringLiteral("Edit order"), QStringLiteral("ghostButton"), this);
    connect(editBtn, &QPushButton::clicked, this, &OrdersPage::onEditOrder);
    auto* cancelBtn = makeButton(QStringLiteral("Cancel order"), QStringLiteral("dangerButton"),
                                 this);
    connect(cancelBtn, &QPushButton::clicked, this, &OrdersPage::onCancelOrder);
    auto* newBtn = makeButton(QStringLiteral("+  New order"), QStringLiteral("primaryButton"), this);
    newBtn->setToolTip(QStringLiteral("Open a new order (Ctrl+N)"));
    connect(newBtn, &QPushButton::clicked, this, &OrdersPage::onNewOrder);

    headerRow->addWidget(splitBtn);
    headerRow->addWidget(mergeBtn);
    headerRow->addWidget(editBtn);
    headerRow->addWidget(cancelBtn);
    headerRow->addWidget(newBtn);
    root->addLayout(headerRow);

    // --- three working panes -------------------------------------------------
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("posSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(14);

    // Pane 1: the floor — active orders on top, the kitchen pass underneath.
    auto* leftSplitter = new QSplitter(Qt::Vertical, splitter);
    leftSplitter->setObjectName(QStringLiteral("posLeftSplitter"));
    leftSplitter->setChildrenCollapsible(false);
    leftSplitter->setHandleWidth(14);

    QFrame* ordersPanel = nullptr;
    QVBoxLayout* ordersColumn = buildPanel(QStringLiteral("Active Orders"), leftSplitter,
                                           &ordersPanel);
    // The order number is the primary identifier of this whole screen — "ORD-20…01-007" is not an
    // order number, it is a shrug. It therefore takes the stretch column and every pixel of slack
    // the pane has, while the four short columns size themselves to their content; the pane below
    // is seeded wide enough that a 16-character number never has to give a character up.
    m_orderList = new WholeRowTable(
        QStringList{QStringLiteral("Order"), QStringLiteral("Type"), QStringLiteral("Table"),
                    QStringLiteral("Status"), QStringLiteral("Total")},
        ordersPanel);
    m_orderList->setSortingEnabled(false);
    // No corner box: the vertical header is hidden, so QTableCornerButton would only ever render
    // as a bare grey square in the bottom-right corner that reads as a stray checkbox.
    m_orderList->setCornerButtonEnabled(false);
    // Deliberately NOT ScrollBarAlwaysOff: four of the five columns are content-sized, so a window
    // dragged to its absolute minimum can still want more width than it has, and suppressing the
    // bar there would leave the Total column clipped and unreachable rather than merely off-screen.
    // Should the window ever be dragged narrower than the order number, lose the middle rather
    // than the tail: the trailing serial is the part a waiter actually reads out.
    m_orderList->setTextElideMode(Qt::ElideMiddle);
    m_orderList->horizontalHeader()->setStretchLastSection(false);
    m_orderList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_orderList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_orderList->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_orderList->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_orderList->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    ordersColumn->addWidget(m_orderList, 1);
    leftSplitter->addWidget(ordersPanel);

    QFrame* kitchenPanel = nullptr;
    QVBoxLayout* kitchenColumn = buildPanel(QStringLiteral("Kitchen Pass"), leftSplitter,
                                            &kitchenPanel,
                                            QStringLiteral("Pending → Preparing → Ready"));
    m_kitchenBoard = new QListWidget(kitchenPanel);
    m_kitchenBoard->setObjectName(QStringLiteral("kitchenBoard"));
    m_kitchenBoard->setFrameShape(QFrame::NoFrame);
    m_kitchenBoard->setSelectionMode(QAbstractItemView::NoSelection);
    m_kitchenBoard->setFocusPolicy(Qt::NoFocus);
    // A long ticket line elides; it never widens the board into a sideways scroll (which would
    // also drag a grey scroll-area corner box in with it).
    m_kitchenBoard->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_kitchenBoard->setTextElideMode(Qt::ElideRight);
    m_kitchenBoard->setWordWrap(false);
    kitchenColumn->addWidget(m_kitchenBoard, 1);
    leftSplitter->addWidget(kitchenPanel);
    // The pass is sized to the tickets it is actually holding (see fitKitchenPanel): an empty
    // pass is a compact one-line card and the floor list takes the height instead, rather than a
    // half-empty panel showing a scrollbar over three identical placeholders.
    leftSplitter->setStretchFactor(0, 5);
    leftSplitter->setStretchFactor(1, 0);
    splitter->addWidget(leftSplitter);

    // Pane 2: the menu picker.
    QFrame* pickerPanel = nullptr;
    QVBoxLayout* pickerColumn = buildPanel(QStringLiteral("Menu"), splitter, &pickerPanel,
                                           QStringLiteral("Only dishes the kitchen is serving"));

    auto* pickerFilters = new QHBoxLayout();
    pickerFilters->setSpacing(8);
    auto* pickerCategory = new QComboBox(pickerPanel);
    pickerCategory->setObjectName(QStringLiteral("posCategory"));
    pickerCategory->setMinimumHeight(36);
    pickerCategory->setCursor(Qt::PointingHandCursor);
    pickerFilters->addWidget(pickerCategory, 1);
    pickerColumn->addLayout(pickerFilters);

    auto* pickerSearch = new SearchBar(QStringLiteral("Search a dish…"), pickerPanel);
    tagWidget(pickerSearch, QStringLiteral("posSearch"));
    pickerColumn->addWidget(pickerSearch);

    auto* pickerList = new WholeRowListWidget(pickerPanel);
    pickerList->setObjectName(QStringLiteral("posItems"));
    pickerList->setFrameShape(QFrame::NoFrame);
    pickerList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // Every row elides itself to the viewport, so sideways scrolling is never needed. Switching
    // the bar off also removes the scroll-area corner box that used to sit under it.
    pickerList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pickerList->setResizeMode(QListView::Adjust);  // re-lay the rows out when the pane is resized
    pickerList->setWordWrap(false);
    pickerList->setUniformItemSizes(false);
    pickerList->setMinimumHeight(200);
    pickerList->setItemDelegate(new PickerRowDelegate(pickerList));
    pickerColumn->addWidget(pickerList, 1);

    auto* addRow = new QHBoxLayout();
    addRow->setSpacing(8);
    auto* qty = new QSpinBox(pickerPanel);
    qty->setObjectName(QStringLiteral("posQty"));
    qty->setRange(1, 99);
    qty->setValue(1);
    qty->setMinimumHeight(36);
    // 12px of QSS padding either side, a 2px border and a 27px stepper gutter — "x 99" needs
    // this much before the prefix starts getting sliced.
    qty->setFixedWidth(92);
    qty->setPrefix(QStringLiteral("x "));
    auto* addToOrderBtn = makeButton(QStringLiteral("Add to order"),
                                     QStringLiteral("primaryButton"), pickerPanel);
    // The caption must never be sliced by the button edge: 13px semibold + the 18px QSS padding
    // needs this much room even when the pane is at its narrowest.
    addToOrderBtn->setMinimumWidth(140);
    connect(addToOrderBtn, &QPushButton::clicked, this, &OrdersPage::onAddItemToOrder);
    addRow->addWidget(qty, 0);
    addRow->addWidget(addToOrderBtn, 1);
    pickerColumn->addLayout(addRow);
    splitter->addWidget(pickerPanel);

    // Pane 3: the ticket.
    QFrame* ticketPanel = nullptr;
    QVBoxLayout* ticketColumn = buildPanel(QStringLiteral("Current Ticket"), splitter,
                                           &ticketPanel);

    auto* ticketHeader = styledLabel(QString(), QStringLiteral("mutedLabel"), ticketPanel);
    tagWidget(ticketHeader, QStringLiteral("ticketHeader"));
    ticketHeader->setWordWrap(true);
    ticketColumn->addWidget(ticketHeader);

    auto* ticketNote = styledLabel(QString(), QStringLiteral("mutedLabel"), ticketPanel, -1);
    tagWidget(ticketNote, QStringLiteral("ticketNote"));
    ticketNote->setWordWrap(true);
    ticketNote->setVisible(false);
    ticketColumn->addWidget(ticketNote);

    // Two columns, not four. A ticket line is one dish and one amount; the quantity and the unit
    // price are a caption under the dish name (see fillTicket), which is both how a printed
    // restaurant bill reads and the only way the dish name gets a column wide enough to be read
    // in a pane this size. No information was dropped — it moved to where there is room for it.
    m_itemsView = new ElegantTable(
        QStringList{QStringLiteral("Item"), QStringLiteral("Amount")}, ticketPanel);
    m_itemsView->setSortingEnabled(false);
    m_itemsView->setSelectionMode(QAbstractItemView::ExtendedSelection);  // split needs multi-select
    m_itemsView->setCornerButtonEnabled(false);  // no bare grey square in the bottom-right corner
    m_itemsView->setWordWrap(false);
    m_itemsView->setItemDelegate(new PickerRowDelegate(m_itemsView));
    // Tall enough for the delegate's two lines plus its own padding; anything shorter and the
    // caption would be clipped rather than centred.
    m_itemsView->verticalHeader()->setDefaultSectionSize(58);
    m_itemsView->horizontalHeader()->setStretchLastSection(false);
    m_itemsView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_itemsView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ticketColumn->addWidget(m_itemsView, 1);

    auto* totalRow = new QHBoxLayout();
    totalRow->setSpacing(10);
    totalRow->addWidget(styledLabel(QStringLiteral("Total (incl. tax)"),
                                    QStringLiteral("mutedLabel"), ticketPanel),
                        1);
    auto* ticketTotal = styledLabel(core::formatNpr(core::Money::zero()),
                                    QStringLiteral("statCardValue"), ticketPanel, 8,
                                    QFont::DemiBold);
    tagWidget(ticketTotal, QStringLiteral("ticketTotal"));
    ticketTotal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    tint(ticketTotal, ThemeManager::instance().palette().primary);
    totalRow->addWidget(ticketTotal, 0);
    ticketColumn->addLayout(totalRow);

    // Four captions on one line cannot survive a narrow ticket pane — a QPushButton clips its
    // text rather than eliding it, which is how "Send to kitchen" became "nd to kitch". Two rows
    // of two always fit, and it puts the settle action in the natural bottom-right corner.
    auto* ticketActions = new QGridLayout();
    ticketActions->setHorizontalSpacing(8);
    ticketActions->setVerticalSpacing(8);
    auto* removeBtn = makeButton(QStringLiteral("Remove line"), QStringLiteral("ghostButton"),
                                 ticketPanel);
    connect(removeBtn, &QPushButton::clicked, this, &OrdersPage::onRemoveItemFromOrder);
    auto* fireBtn = makeButton(QStringLiteral("Send to kitchen"), QStringLiteral("ghostButton"),
                               ticketPanel);
    connect(fireBtn, &QPushButton::clicked, this, &OrdersPage::onSubmitToKitchen);
    auto* advanceBtn = makeButton(QStringLiteral("Advance status"), QStringLiteral("ghostButton"),
                                  ticketPanel);
    tagWidget(advanceBtn, QStringLiteral("advanceButton"));
    connect(advanceBtn, &QPushButton::clicked, this, &OrdersPage::onAdvanceStatus);
    // "&&" — a single ampersand is swallowed by Qt as a mnemonic and renders as "Bill_settle".
    auto* billBtn = makeButton(QStringLiteral("Bill && settle"), QStringLiteral("primaryButton"),
                               ticketPanel);
    connect(billBtn, &QPushButton::clicked, this, &OrdersPage::onBill);

    ticketActions->addWidget(removeBtn, 0, 0);
    ticketActions->addWidget(fireBtn, 0, 1);
    ticketActions->addWidget(advanceBtn, 1, 0);
    ticketActions->addWidget(billBtn, 1, 1);
    ticketActions->setColumnStretch(0, 1);
    ticketActions->setColumnStretch(1, 1);
    ticketColumn->addLayout(ticketActions);
    splitter->addWidget(ticketPanel);

    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 5);
    // Minimums add up to just under the narrowest MainWindow (1140 - 232 rail - margins), so no
    // pane is ever squeezed into clipping its own content.
    leftSplitter->setMinimumWidth(320);
    pickerPanel->setMinimumWidth(280);
    ticketPanel->setMinimumWidth(230);
    // Stretch factors alone only govern *extra* space, and the panes' own size hints skewed the
    // opening layout badly enough that the floor pane could not show a whole order number. Seed
    // the split explicitly instead; QSplitter scales these proportionally to whatever width it
    // actually gets. The numbers are the measured width each pane needs to show its widest real
    // content untruncated: a 16-character order number plus three short columns on the left, a
    // full dish name in the picker, a full dish name plus an amount on the ticket.
    splitter->setSizes(QList<int>{590, 295, 380});
    leftSplitter->setSizes(QList<int>{400, 160});
    root->addWidget(splitter, 1);

    // --- wiring --------------------------------------------------------------
    connect(m_orderList, &QTableWidget::itemSelectionChanged, this, [this]() {
        fillTicket(m_ctx, this, m_itemsView, selectedOrderId());
    });
    connect(pickerList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { onAddItemToOrder(); });
    connect(pickerCategory, &QComboBox::currentIndexChanged, this,
            [this](int) { populatePicker(m_ctx, this); });
    connect(pickerSearch, &QLineEdit::textChanged, this,
            [this](const QString&) { populatePicker(m_ctx, this); });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &OrdersPage::refresh);

    refresh();
}

QString OrdersPage::pageTitle() const { return QStringLiteral("Orders"); }

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void OrdersPage::refresh() {
    const Palette& pal = ThemeManager::instance().palette();
    try {
        // --- categories for the picker (only rebuilt when they are missing) ---
        if (auto* pickerCategory = findChild<QComboBox*>(QStringLiteral("posCategory"))) {
            const QString previous = pickerCategory->currentData().toString();
            const QSignalBlocker block(pickerCategory);
            pickerCategory->clear();
            pickerCategory->addItem(QStringLiteral("All sections"), QString());
            for (const QString& c : m_ctx.menu().categories()) pickerCategory->addItem(c, c);
            const int index = previous.isEmpty() ? 0 : pickerCategory->findData(previous);
            pickerCategory->setCurrentIndex(index >= 0 ? index : 0);
        }
        populatePicker(m_ctx, this);

        // --- active orders ----------------------------------------------------
        const int previouslySelected = selectedOrderId();
        std::vector<models::Order> active = m_ctx.orders().activeOrders();
        std::sort(active.begin(), active.end(),
                  [](const models::Order& a, const models::Order& b) {
                      return a.createdAt() > b.createdAt();
                  });

        QHash<int, QString> tableNames;
        for (const models::Table& t : m_ctx.reservations().tables())
            tableNames.insert(t.id(), t.name());

        int restoreRow = -1;
        {
            const QSignalBlocker block(m_orderList);
            m_orderList->clearSpans();  // drop any previous empty-state span
            m_orderList->setRowCount(0);
            int row = 0;
            for (const models::Order& order : active) {
                m_orderList->insertRow(row);

                auto* numberCell = new QTableWidgetItem(order.orderNumber());
                numberCell->setData(kIdRole, order.id());
                QFont nf = numberCell->font();
                nf.setWeight(QFont::DemiBold);
                numberCell->setFont(nf);
                m_orderList->setItem(row, 0, numberCell);

                auto* typeCell = new QTableWidgetItem(typeText(order.type()));
                typeCell->setForeground(pal.textMuted);
                m_orderList->setItem(row, 1, typeCell);

                auto* tableCell = new QTableWidgetItem(
                    order.tableId() > 0
                        ? tableNames.value(order.tableId(),
                                           QStringLiteral("#%1").arg(order.tableId()))
                        : QStringLiteral("—"));
                tableCell->setTextAlignment(Qt::AlignCenter);
                tableCell->setForeground(pal.textMuted);
                m_orderList->setItem(row, 2, tableCell);

                auto* statusCell = new QTableWidgetItem(statusText(order.status()));
                statusCell->setForeground(statusColour(order.status(), pal));
                QFont sf = statusCell->font();
                sf.setWeight(QFont::DemiBold);
                statusCell->setFont(sf);
                m_orderList->setItem(row, 3, statusCell);

                auto* totalCell = new QTableWidgetItem(core::formatNpr(order.subtotal()));
                totalCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                QFont tf = totalCell->font();
                tf.setWeight(QFont::DemiBold);
                totalCell->setFont(tf);
                m_orderList->setItem(row, 4, totalCell);

                if (order.id() == previouslySelected) restoreRow = row;
                ++row;
            }
            if (row == 0) {
                m_orderList->insertRow(0);
                auto* empty = new QTableWidgetItem(
                    QStringLiteral("No orders open — press “New order” to start service."));
                empty->setTextAlignment(Qt::AlignCenter);
                empty->setForeground(pal.textMuted);
                empty->setFlags(Qt::NoItemFlags);
                m_orderList->setItem(0, 0, empty);
                m_orderList->setSpan(0, 0, 1, m_orderList->columnCount());
                m_orderList->setRowHeight(0, 84);
            } else if (restoreRow < 0) {
                restoreRow = 0;
            }
        }
        if (restoreRow >= 0) m_orderList->selectRow(restoreRow);

        // --- kitchen board ----------------------------------------------------
        // Read the three rungs first, then decide what the board should say. An empty pass gets
        // ONE honest line — three stacked "— clear —" placeholders under three headings read as
        // a broken widget, not as a quiet kitchen, and they used to overflow the panel into a
        // scrollbar over content that fits.
        const models::OrderStatus board[3] = {models::OrderStatus::Pending,
                                              models::OrderStatus::Preparing,
                                              models::OrderStatus::Ready};
        std::vector<models::Order> queue[3];
        std::size_t ticketsOnPass = 0;
        for (int rung = 0; rung < 3; ++rung) {
            queue[rung] = m_ctx.orders().withStatus(board[rung]);
            ticketsOnPass += queue[rung].size();
        }

        m_kitchenBoard->clear();
        if (ticketsOnPass == 0) {
            auto* none = new QListWidgetItem(
                QStringLiteral("Nothing on the pass — no ticket has been fired yet."),
                m_kitchenBoard);
            none->setFlags(Qt::NoItemFlags);
            none->setTextAlignment(Qt::AlignCenter);
            none->setForeground(pal.textMuted);
            none->setSizeHint(QSize(0, 46));
        } else {
            for (int rung = 0; rung < 3; ++rung) {
                auto* heading =
                    new QListWidgetItem(statusText(board[rung]).toUpper(), m_kitchenBoard);
                heading->setFlags(Qt::NoItemFlags);
                heading->setForeground(pal.textMuted);
                QFont hf = heading->font();
                hf.setWeight(QFont::DemiBold);
                hf.setPointSize(std::max(8, hf.pointSize() - 1));
                heading->setFont(hf);
                heading->setSizeHint(QSize(0, 30));

                if (queue[rung].empty()) {
                    // Meaningful here, because the rungs either side of it are carrying tickets.
                    auto* none =
                        new QListWidgetItem(QStringLiteral("    — clear —"), m_kitchenBoard);
                    none->setFlags(Qt::NoItemFlags);
                    none->setForeground(pal.textMuted);
                    none->setSizeHint(QSize(0, 26));
                    continue;
                }
                for (const models::Order& ticket : queue[rung]) {
                    QString label =
                        QStringLiteral("    %1  ·  %2 item%3")
                            .arg(ticket.orderNumber())
                            .arg(ticket.itemCount())
                            .arg(ticket.itemCount() == 1 ? QString() : QStringLiteral("s"));
                    if (ticket.tableId() > 0)
                        label += QStringLiteral("  ·  %1")
                                     .arg(tableNames.value(
                                         ticket.tableId(),
                                         QStringLiteral("#%1").arg(ticket.tableId())));
                    auto* entry = new QListWidgetItem(label, m_kitchenBoard);
                    entry->setFlags(Qt::NoItemFlags);
                    entry->setForeground(statusColour(board[rung], pal));
                    entry->setSizeHint(QSize(0, 28));
                    if (!ticket.note().isEmpty()) entry->setToolTip(ticket.note());
                }
            }
        }
        fitKitchenPanel(this, m_kitchenBoard);

        // --- the ticket -------------------------------------------------------
        fillTicket(m_ctx, this, m_itemsView, selectedOrderId());
    } catch (const std::exception& e) {
        // refresh() must never throw (Page contract).
        m_ctx.notifications().notify(QStringLiteral("Orders"), QString::fromUtf8(e.what()), 3);
    }
}

// ---------------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------------

int OrdersPage::selectedOrderId() const {
    const int row = m_orderList->currentRow();
    if (row < 0) return 0;
    const QTableWidgetItem* cell = m_orderList->item(row, 0);
    if (!cell) return 0;
    return cell->data(kIdRole).toInt();
}

int OrdersPage::selectedLineIndex() const {
    const QModelIndexList selected = m_itemsView->selectionModel()->selectedRows();
    const int row = selected.isEmpty() ? m_itemsView->currentRow() : selected.first().row();
    if (row < 0) return -1;
    // The empty-state placeholder spans every column and therefore has no quantity cell.
    return m_itemsView->item(row, 1) ? row : -1;
}

// ---------------------------------------------------------------------------
// Order lifecycle
// ---------------------------------------------------------------------------

void OrdersPage::onNewOrder() {
    OrderHeaderChoice choice;
    if (!orderHeaderDialog(this, m_ctx, choice, /*editing=*/false)) return;

    const int waiterId =
        m_ctx.auth().currentUser() ? m_ctx.auth().currentUser()->employeeId() : 0;
    const core::Result<models::Order> created =
        m_ctx.orders().createOrder(choice.type, choice.tableId, choice.customerId, waiterId);
    if (created.isErr()) {
        m_ctx.notifications().notify(QStringLiteral("Could not open the order"), created.error(), 3);
        return;
    }

    const models::Order& order = created.value();
    if (!choice.note.isEmpty()) {
        const core::Result<void> noted = m_ctx.orders().setOrderNote(order.id(), choice.note);
        if (noted.isErr())
            m_ctx.notifications().notify(QStringLiteral("Note not saved"), noted.error(), 2);
    }

    m_ctx.notifications().notify(QStringLiteral("Order opened"),
                                 QStringLiteral("%1 is ready for items.").arg(order.orderNumber()),
                                 1);
    refresh();

    // Select the brand-new order so the very next tap lands on the right ticket.
    for (int row = 0; row < m_orderList->rowCount(); ++row) {
        const QTableWidgetItem* cell = m_orderList->item(row, 0);
        if (cell && cell->data(kIdRole).toInt() == order.id()) {
            m_orderList->selectRow(row);
            break;
        }
    }
    if (auto* search = findTagged<QLineEdit*>(this, QStringLiteral("posSearch"))) search->setFocus();
}

void OrdersPage::onEditOrder() {
    const int orderId = selectedOrderId();
    if (orderId == 0) {
        m_ctx.notifications().notify(QStringLiteral("Orders"),
                                     QStringLiteral("Select an order first."), 0);
        return;
    }
    const std::optional<models::Order> order = m_ctx.orders().order(orderId);
    if (!order.has_value()) {
        m_ctx.notifications().notify(QStringLiteral("Orders"),
                                     QStringLiteral("That order no longer exists."), 2);
        refresh();
        return;
    }

    OrderHeaderChoice choice;
    choice.type = order->type();
    choice.tableId = order->tableId();
    choice.customerId = order->customerId();
    choice.note = order->note();
    if (!orderHeaderDialog(this, m_ctx, choice, /*editing=*/true)) return;

    const core::Result<void> saved = m_ctx.orders().setOrderNote(orderId, choice.note);
    if (saved.isErr()) {
        m_ctx.notifications().notify(QStringLiteral("Could not save the order"), saved.error(), 3);
        return;
    }
    m_ctx.notifications().notify(QStringLiteral("Order updated"),
                                 QStringLiteral("%1 saved.").arg(order->orderNumber()), 1);
    refresh();
}

void OrdersPage::onCancelOrder() {
    const int orderId = selectedOrderId();
    if (orderId == 0) {
        m_ctx.notifications().notify(QStringLiteral("Orders"),
                                     QStringLiteral("Select an order to cancel."), 0);
        return;
    }
    const std::optional<models::Order> order = m_ctx.orders().order(orderId);
    if (!order.has_value()) return;

    const auto answer = QMessageBox::question(
        this, QStringLiteral("Cancel order"),
        QStringLiteral("Cancel %1?\n\nIt is taken off the kitchen pass and cannot be reopened.")
            .arg(order->orderNumber()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    const core::Result<void> cancelled = m_ctx.orders().cancelOrder(orderId);
    if (cancelled.isErr()) {
        m_ctx.notifications().notify(QStringLiteral("Could not cancel"), cancelled.error(), 3);
        return;
    }
    m_ctx.notifications().notify(QStringLiteral("Order cancelled"),
                                 QStringLiteral("%1 has been voided.").arg(order->orderNumber()), 1);
    refresh();
}

// ---------------------------------------------------------------------------
// Line editing — every change is an undoable Command
// ---------------------------------------------------------------------------

void OrdersPage::onAddItemToOrder() {
    const int orderId = selectedOrderId();
    if (orderId == 0) {
        m_ctx.notifications().notify(QStringLiteral("Orders"),
                                     QStringLiteral("Open or select an order first."), 0);
        return;
    }
    auto* list = findChild<QListWidget*>(QStringLiteral("posItems"));
    auto* qty = findChild<QSpinBox*>(QStringLiteral("posQty"));
    QListWidgetItem* chosen = list ? list->currentItem() : nullptr;
    if (!chosen || chosen->data(kIdRole).toInt() == 0) {
        m_ctx.notifications().notify(QStringLiteral("Menu"),
                                     QStringLiteral("Pick a dish from the menu list."), 0);
        return;
    }

    const int menuItemId = chosen->data(kIdRole).toInt();
    const int quantity = qty ? qty->value() : 1;

    /// @oop-concept Runtime Polymorphism :: the edit is pushed as an abstract services::Command,
    /// which is what makes Ctrl+Z work for a mis-tapped dish during service
    auto command = std::make_unique<services::AddOrderItemCommand>(m_ctx.orders(), orderId,
                                                                   menuItemId, quantity);
    const core::Result<void> result = m_ctx.commands().run(std::move(command));
    if (result.isErr()) {
        m_ctx.notifications().notify(QStringLiteral("Could not add the dish"), result.error(), 3);
        return;
    }
    if (qty) qty->setValue(1);
    refresh();
}

void OrdersPage::onRemoveItemFromOrder() {
    const int orderId = selectedOrderId();
    const int line = selectedLineIndex();
    if (orderId == 0 || line < 0) {
        m_ctx.notifications().notify(QStringLiteral("Orders"),
                                     QStringLiteral("Select a line on the ticket first."), 0);
        return;
    }

    auto command = std::make_unique<services::RemoveOrderItemCommand>(
        m_ctx.orders(), orderId, static_cast<std::size_t>(line));
    const core::Result<void> result = m_ctx.commands().run(std::move(command));
    if (result.isErr()) {
        m_ctx.notifications().notify(QStringLiteral("Could not remove the line"), result.error(), 3);
        return;
    }
    refresh();
}

// ---------------------------------------------------------------------------
// Kitchen pipeline
// ---------------------------------------------------------------------------

void OrdersPage::onSubmitToKitchen() {
    const int orderId = selectedOrderId();
    if (orderId == 0) {
        m_ctx.notifications().notify(QStringLiteral("Orders"),
                                     QStringLiteral("Select an order to fire."), 0);
        return;
    }
    const core::Result<void> fired = m_ctx.orders().submitToKitchen(orderId);
    if (fired.isErr()) {
        m_ctx.notifications().notify(QStringLiteral("Could not send to the kitchen"), fired.error(),
                                     3);
        return;
    }
    m_ctx.notifications().notify(QStringLiteral("Sent to the kitchen"),
                                 QStringLiteral("The ticket is on the pass."), 1);
    refresh();
}

void OrdersPage::onAdvanceStatus() {
    const int orderId = selectedOrderId();
    if (orderId == 0) {
        m_ctx.notifications().notify(QStringLiteral("Orders"),
                                     QStringLiteral("Select an order to advance."), 0);
        return;
    }
    const core::Result<void> advanced = m_ctx.orders().advanceStatus(orderId);
    if (advanced.isErr()) {
        m_ctx.notifications().notify(QStringLiteral("Could not advance the order"),
                                     advanced.error(), 3);
        return;
    }
    const std::optional<models::Order> order = m_ctx.orders().order(orderId);
    if (order.has_value()) {
        m_ctx.notifications().notify(
            QStringLiteral("Order advanced"),
            QStringLiteral("%1 is now %2.")
                .arg(order->orderNumber(), statusText(order->status()).toLower()),
            1);
    }
    refresh();
}

// ---------------------------------------------------------------------------
// Split and merge — the two OOP mechanics a guest actually sees
// ---------------------------------------------------------------------------

void OrdersPage::onSplit() {
    const int orderId = selectedOrderId();
    if (orderId == 0) {
        m_ctx.notifications().notify(QStringLiteral("Orders"),
                                     QStringLiteral("Select the order to split."), 0);
        return;
    }

    const QModelIndexList selected = m_itemsView->selectionModel()->selectedRows();
    std::vector<std::size_t> indexes;
    for (const QModelIndex& index : selected) {
        if (m_itemsView->item(index.row(), 1) == nullptr) continue;  // the empty-state row
        indexes.push_back(static_cast<std::size_t>(index.row()));
    }
    if (indexes.empty()) {
        m_ctx.notifications().notify(
            QStringLiteral("Split bill"),
            QStringLiteral("Select the lines that move onto the second bill "
                           "(hold Cmd or Shift to pick several)."),
            0);
        return;
    }
    std::sort(indexes.begin(), indexes.end());

    /// @oop-concept Copy Constructor (surfaced) :: "split bill" exists in this UI only because
    /// models::Order can be deep-copied into a new, unsaved order with its own item vector
    const core::Result<models::Order> split = m_ctx.orders().splitOrder(orderId, indexes);
    if (split.isErr()) {
        m_ctx.notifications().notify(QStringLiteral("Could not split the bill"), split.error(), 3);
        return;
    }
    m_ctx.notifications().notify(
        QStringLiteral("Bill split"),
        QStringLiteral("%1 line%2 moved onto %3.")
            .arg(indexes.size())
            .arg(indexes.size() == 1 ? QString() : QStringLiteral("s"),
                 split.value().orderNumber()),
        1);
    refresh();
}

void OrdersPage::onMerge() {
    const int targetId = selectedOrderId();
    if (targetId == 0) {
        m_ctx.notifications().notify(
            QStringLiteral("Merge bills"),
            QStringLiteral("Select the order everything should end up on."), 0);
        return;
    }

    std::vector<models::Order> candidates;
    for (models::Order& order : m_ctx.orders().activeOrders()) {
        if (order.id() != targetId) candidates.push_back(std::move(order));
    }
    if (candidates.empty()) {
        m_ctx.notifications().notify(QStringLiteral("Merge bills"),
                                     QStringLiteral("There is no other open order to merge in."), 0);
        return;
    }

    const std::optional<models::Order> target = m_ctx.orders().order(targetId);
    const QString targetNumber = target.has_value() ? target->orderNumber() : QString::number(targetId);
    const int sourceId = pickOtherOrderDialog(
        this, candidates, QStringLiteral("Merge bills"),
        QStringLiteral("Its lines move onto %1 and the chosen order is cancelled.").arg(targetNumber));
    if (sourceId == 0) return;

    /// @oop-concept Operator Overloading (surfaced) :: "merge bills" IS models::Order::operator+=
    const core::Result<void> merged = m_ctx.orders().mergeOrders(targetId, sourceId);
    if (merged.isErr()) {
        m_ctx.notifications().notify(QStringLiteral("Could not merge"), merged.error(), 3);
        return;
    }
    m_ctx.notifications().notify(QStringLiteral("Bills merged"),
                                 QStringLiteral("Everything is now on %1.").arg(targetNumber), 1);
    refresh();
}

// ---------------------------------------------------------------------------
// Billing
// ---------------------------------------------------------------------------

void OrdersPage::onBill() {
    const int orderId = selectedOrderId();
    if (orderId == 0) {
        m_ctx.notifications().notify(QStringLiteral("Billing"),
                                     QStringLiteral("Select the order to settle."), 0);
        return;
    }
    const std::optional<models::Order> order = m_ctx.orders().order(orderId);
    if (!order.has_value()) {
        refresh();
        return;
    }
    if (order->status() != models::OrderStatus::Served) {
        m_ctx.notifications().notify(
            QStringLiteral("Not ready to bill"),
            QStringLiteral("%1 is %2 — an order is billed once it has been served.")
                .arg(order->orderNumber(), statusText(order->status()).toLower()),
            2);
        return;
    }

    BillingDialog dialog(m_ctx, orderId, this);
    dialog.exec();
    refresh();
}

} // namespace aluchop::gui

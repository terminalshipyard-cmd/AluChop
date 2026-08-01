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
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QModelIndex>
#include <QModelIndexList>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>
#include <QSplitter>
#include <QString>
#include <QStringList>
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

constexpr int kIdRole = Qt::UserRole;  ///< order id / menu-item id carried by a row

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

/// A picker row: dish name on the left, tax-inclusive price on the right.
QWidget* pickerRow(const models::MenuItem& item, const Palette& pal, QWidget* parent) {
    auto* row = new QWidget(parent);
    row->setAutoFillBackground(false);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(4, 5, 4, 5);
    h->setSpacing(10);

    auto* name = new QLabel(item.name(), row);
    QFont nf = name->font();
    nf.setWeight(QFont::DemiBold);
    name->setFont(nf);

    auto* category = new QLabel(item.category(), row);
    QPalette cp = category->palette();
    cp.setColor(QPalette::WindowText, pal.textMuted);
    category->setPalette(cp);
    QFont cf = category->font();
    cf.setPointSize(std::max(8, cf.pointSize() - 1));
    category->setFont(cf);

    auto* left = new QVBoxLayout();
    left->setSpacing(0);
    left->addWidget(name);
    left->addWidget(category);

    auto* price = new QLabel(core::formatNpr(item.price()), row);
    price->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont pf = price->font();
    pf.setWeight(QFont::DemiBold);
    price->setFont(pf);
    QPalette pp = price->palette();
    pp.setColor(QPalette::WindowText, pal.primary);
    price->setPalette(pp);

    h->addLayout(left, 1);
    h->addWidget(price, 0);
    return row;
}

/// Centred "nothing here" row for a list widget.
void addEmptyRow(QListWidget* list, const QString& text, const QColor& colour) {
    auto* item = new QListWidgetItem(text, list);
    item->setFlags(Qt::NoItemFlags);
    item->setTextAlignment(Qt::AlignCenter);
    item->setForeground(colour);
    item->setSizeHint(QSize(0, 60));
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
        auto* entry = new QListWidgetItem(list);
        entry->setData(kIdRole, item.id());
        entry->setToolTip(item.description().isEmpty() ? item.name() : item.description());
        auto* row = pickerRow(item, pal, list);
        entry->setSizeHint(row->sizeHint().expandedTo(QSize(0, 46)));
        list->setItemWidget(entry, row);
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

        auto* nameCell = new QTableWidgetItem(line.name());
        QFont nf = nameCell->font();
        nf.setWeight(QFont::DemiBold);
        nameCell->setFont(nf);
        if (!line.note().isEmpty()) nameCell->setToolTip(line.note());
        itemsView->setItem(row, 0, nameCell);

        auto* qtyCell = new QTableWidgetItem(QStringLiteral("x%1").arg(line.qty()));
        qtyCell->setTextAlignment(Qt::AlignCenter);
        itemsView->setItem(row, 1, qtyCell);

        auto* unitCell = new QTableWidgetItem(core::formatNpr(line.unitPrice()));
        unitCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        unitCell->setForeground(pal.textMuted);
        itemsView->setItem(row, 2, unitCell);

        auto* lineCell = new QTableWidgetItem(core::formatNpr(line.lineTotal()));
        lineCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        itemsView->setItem(row, 3, lineCell);

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
    leftSplitter->setChildrenCollapsible(false);
    leftSplitter->setHandleWidth(14);

    QFrame* ordersPanel = nullptr;
    QVBoxLayout* ordersColumn = buildPanel(QStringLiteral("Active Orders"), leftSplitter,
                                           &ordersPanel);
    m_orderList = new ElegantTable(
        QStringList{QStringLiteral("Order"), QStringLiteral("Type"), QStringLiteral("Table"),
                    QStringLiteral("Status"), QStringLiteral("Total")},
        ordersPanel);
    m_orderList->setSortingEnabled(false);
    m_orderList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_orderList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_orderList->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_orderList->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
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
    kitchenColumn->addWidget(m_kitchenBoard, 1);
    leftSplitter->addWidget(kitchenPanel);
    leftSplitter->setStretchFactor(0, 3);
    leftSplitter->setStretchFactor(1, 2);
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

    auto* pickerList = new QListWidget(pickerPanel);
    pickerList->setObjectName(QStringLiteral("posItems"));
    pickerList->setFrameShape(QFrame::NoFrame);
    pickerList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    pickerColumn->addWidget(pickerList, 1);

    auto* addRow = new QHBoxLayout();
    addRow->setSpacing(8);
    auto* qty = new QSpinBox(pickerPanel);
    qty->setObjectName(QStringLiteral("posQty"));
    qty->setRange(1, 99);
    qty->setValue(1);
    qty->setMinimumHeight(36);
    qty->setPrefix(QStringLiteral("x "));
    auto* addToOrderBtn = makeButton(QStringLiteral("Add to order"),
                                     QStringLiteral("primaryButton"), pickerPanel);
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

    m_itemsView = new ElegantTable(
        QStringList{QStringLiteral("Item"), QStringLiteral("Qty"), QStringLiteral("Unit"),
                    QStringLiteral("Line total")},
        ticketPanel);
    m_itemsView->setSortingEnabled(false);
    m_itemsView->setSelectionMode(QAbstractItemView::ExtendedSelection);  // split needs multi-select
    m_itemsView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_itemsView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_itemsView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_itemsView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
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

    auto* ticketActions = new QHBoxLayout();
    ticketActions->setSpacing(8);
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
    auto* billBtn = makeButton(QStringLiteral("Bill & settle"), QStringLiteral("primaryButton"),
                               ticketPanel);
    connect(billBtn, &QPushButton::clicked, this, &OrdersPage::onBill);

    ticketActions->addWidget(removeBtn);
    ticketActions->addWidget(fireBtn);
    ticketActions->addWidget(advanceBtn);
    ticketActions->addWidget(billBtn);
    ticketColumn->addLayout(ticketActions);
    splitter->addWidget(ticketPanel);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 4);
    leftSplitter->setMinimumWidth(300);
    pickerPanel->setMinimumWidth(260);
    ticketPanel->setMinimumWidth(320);
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

                m_orderList->setItem(row, 1, new QTableWidgetItem(typeText(order.type())));
                m_orderList->setItem(
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
                m_orderList->setItem(row, 3, statusCell);

                auto* totalCell = new QTableWidgetItem(core::formatNpr(order.subtotal()));
                totalCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
        m_kitchenBoard->clear();
        const models::OrderStatus board[3] = {models::OrderStatus::Pending,
                                              models::OrderStatus::Preparing,
                                              models::OrderStatus::Ready};
        int ticketsOnPass = 0;
        for (const models::OrderStatus status : board) {
            auto* heading = new QListWidgetItem(statusText(status).toUpper(), m_kitchenBoard);
            heading->setFlags(Qt::NoItemFlags);
            heading->setForeground(pal.textMuted);
            QFont hf = heading->font();
            hf.setWeight(QFont::DemiBold);
            hf.setPointSize(std::max(8, hf.pointSize() - 1));
            heading->setFont(hf);
            heading->setSizeHint(QSize(0, 30));

            const std::vector<models::Order> tickets = m_ctx.orders().withStatus(status);
            if (tickets.empty()) {
                auto* none = new QListWidgetItem(QStringLiteral("    — clear —"), m_kitchenBoard);
                none->setFlags(Qt::NoItemFlags);
                none->setForeground(pal.textMuted);
                none->setSizeHint(QSize(0, 26));
                continue;
            }
            for (const models::Order& ticket : tickets) {
                QString label = QStringLiteral("    %1  ·  %2 item%3")
                                    .arg(ticket.orderNumber())
                                    .arg(ticket.itemCount())
                                    .arg(ticket.itemCount() == 1 ? QString() : QStringLiteral("s"));
                if (ticket.tableId() > 0)
                    label += QStringLiteral("  ·  %1")
                                 .arg(tableNames.value(ticket.tableId(),
                                                       QStringLiteral("#%1").arg(ticket.tableId())));
                auto* entry = new QListWidgetItem(label, m_kitchenBoard);
                entry->setFlags(Qt::NoItemFlags);
                entry->setForeground(statusColour(status, pal));
                entry->setSizeHint(QSize(0, 28));
                if (!ticket.note().isEmpty()) entry->setToolTip(ticket.note());
                ++ticketsOnPass;
            }
        }
        if (ticketsOnPass == 0) {
            auto* none = new QListWidgetItem(
                QStringLiteral("Nothing on the pass right now."), m_kitchenBoard);
            none->setFlags(Qt::NoItemFlags);
            none->setTextAlignment(Qt::AlignCenter);
            none->setForeground(pal.textMuted);
            none->setSizeHint(QSize(0, 44));
        }

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

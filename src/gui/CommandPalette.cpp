/**
 * @file CommandPalette.cpp
 * @brief Implementation of the Ctrl+K "search everywhere" overlay.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * One field that reaches the whole application. Every keystroke re-runs four independent lookups
 * — screen names, dishes, customers, live order numbers — scores each candidate with the same
 * fuzzy subsequence matcher, and merges the winners into a single grouped list.
 *
 * The palette deliberately performs no action of its own. Choosing a row emits
 * navigateRequested() or openOrderRequested() and closes; MainWindow decides what happens next.
 * That is what keeps a global search widget free of every business rule and safe to open from
 * anywhere in the shell.
 */

#include "aluchop/gui/CommandPalette.hpp"

#include "aluchop/core/Money.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/models/Customer.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/MenuItem.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/services/AppContext.hpp"
#include "aluchop/services/CustomerService.hpp"
#include "aluchop/services/MenuService.hpp"
#include "aluchop/services/OrderService.hpp"

#include <QColor>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QObject>
#include <QPoint>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <array>
#include <functional>
#include <utility>
#include <vector>

namespace aluchop::gui {
namespace {

/// Item-data roles used to tag each heterogeneous row with what it actually is.
constexpr int kKindRole = Qt::UserRole;      ///< -1 header · 0 page · 1 dish · 2 customer · 3 order
constexpr int kIdRole = Qt::UserRole + 1;    ///< page index or entity id

constexpr int kKindHeader = -1;
constexpr int kKindPage = 0;
constexpr int kKindMenuItem = 1;
constexpr int kKindCustomer = 2;
constexpr int kKindOrder = 3;

constexpr int kPerSourceLimit = 6;           ///< keep the list scannable, never exhaustive

/// The nine screens, in sidebar order — the same order MainWindow builds them in.
/// @oop-concept Object Arrays :: a fixed, ordered vocabulary belongs in a std::array
const std::array<const char*, 9> kPages{"Dashboard", "Menu",         "Orders",
                                        "Customers", "Employees",    "Inventory",
                                        "Reservations", "Reports",   "Settings"};

/// Event forwarder — CommandPalette's frozen header declares no `showEvent` or `keyPressEvent`
/// override, so both are handled through installed filters rather than by widening the contract.
class EventHook : public QObject {
public:
    EventHook(QObject* owner, std::function<bool(QEvent*)> onEvent)
        : QObject(owner), m_onEvent(std::move(onEvent)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (m_onEvent(event)) {
            return true;   // consumed
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<bool(QEvent*)> m_onEvent;
};

/**
 * @brief Fuzzy subsequence score of @p query against @p text.
 *
 * Every character of the query must appear in the haystack, in order but not necessarily
 * adjacently, so "chmo" finds "Chicken Momo". Contiguous runs and word-start hits score higher,
 * which is what puts the obvious answer at the top instead of the alphabetically first one.
 *
 * @return a score where higher is better, or -1 when the query is not a subsequence at all.
 *
 * @oop-concept Function Template alternative :: one scoring rule shared by all four sources —
 *              heterogeneous hits are ranked on a single, comparable scale
 */
int fuzzyScore(const QString& text, const QString& query) {
    if (query.isEmpty()) {
        return 0;
    }
    const QString hay = text.toLower();
    const QString needle = query.toLower();

    int score = 0;
    int run = 0;
    int at = 0;

    for (int q = 0; q < needle.size(); ++q) {
        const QChar wanted = needle.at(q);
        int found = -1;
        for (int h = at; h < hay.size(); ++h) {
            if (hay.at(h) == wanted) {
                found = h;
                break;
            }
        }
        if (found < 0) {
            return -1;
        }
        score += 10;
        if (found == at && q > 0) {
            run += 6;                       // contiguous match — strongly preferred
            score += run;
        } else {
            run = 0;
        }
        if (found == 0 || hay.at(found - 1) == QLatin1Char(' ')) {
            score += 12;                    // start of a word
        }
        score -= (found - at) / 4;          // long skips are weak matches
        at = found + 1;
    }
    // A short haystack that matched entirely is a better hit than a long one that merely contains
    // the letters somewhere.
    score += std::max(0, 24 - static_cast<int>(hay.size()) / 2);
    if (hay.startsWith(needle)) {
        score += 40;
    }
    return score;
}

/// One candidate row, before it becomes a QListWidgetItem.
struct Hit {
    int score = 0;
    int kind = kKindPage;
    int id = 0;
    QString primary;    ///< bold-ish main line
    QString secondary;  ///< quiet trailing detail
};

/// Ranks and truncates one source's candidates.
void rank(std::vector<Hit>& hits, int limit) {
    std::sort(hits.begin(), hits.end(),
              [](const Hit& a, const Hit& b) { return a.score > b.score; });
    if (static_cast<int>(hits.size()) > limit) {
        hits.resize(static_cast<std::size_t>(limit));
    }
}

} // namespace

CommandPalette::CommandPalette(services::AppContext& ctx, QWidget* parent)
    : QDialog(parent), m_ctx(ctx) {
    setWindowTitle(QStringLiteral("Search everywhere"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setModal(true);
    setFixedSize(660, 470);

    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(16, 16, 16, 16);

    auto* panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("commandPalette"));
    panel->setFrameShape(QFrame::NoFrame);
    shell->addWidget(panel);

    auto* shadow = new QGraphicsDropShadowEffect(panel);
    QColor shadowColour = ThemeManager::instance().palette().shadow;
    shadowColour.setAlphaF(0.32f);
    shadow->setColor(shadowColour);
    shadow->setBlurRadius(54);
    shadow->setOffset(0, 16);
    panel->setGraphicsEffect(shadow);

    auto* column = new QVBoxLayout(panel);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    m_input = new QLineEdit(panel);
    m_input->setObjectName(QStringLiteral("paletteInput"));
    m_input->setPlaceholderText(
        QStringLiteral("Search screens, dishes, customers or order numbers…"));
    m_input->setClearButtonEnabled(true);
    column->addWidget(m_input);

    m_results = new QListWidget(panel);
    m_results->setObjectName(QStringLiteral("paletteResults"));
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setUniformItemSizes(false);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_results->setCursor(Qt::PointingHandCursor);
    column->addWidget(m_results, 1);

    auto* hint = new QLabel(
        QStringLiteral("↑ ↓ to move    ·    Enter to open    ·    Esc to close"), panel);
    hint->setObjectName(QStringLiteral("paletteHint"));
    column->addWidget(hint);

    connect(m_input, &QLineEdit::textChanged, this, &CommandPalette::onQueryChanged);
    connect(m_input, &QLineEdit::returnPressed, this, &CommandPalette::onActivated);
    connect(m_results, &QListWidget::itemClicked, this,
            [this](QListWidgetItem*) { onActivated(); });
    connect(m_results, &QListWidget::itemActivated, this,
            [this](QListWidgetItem*) { onActivated(); });

    // Arrow keys typed in the field must drive the list, which is what makes the palette feel
    // like one control instead of two.
    m_input->installEventFilter(new EventHook(this, [this](QEvent* event) {
        if (event->type() != QEvent::KeyPress) {
            return false;
        }
        auto* key = static_cast<QKeyEvent*>(event);
        const int rows = m_results->count();
        if (rows == 0) {
            return false;
        }
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up) {
            const int step = (key->key() == Qt::Key_Down) ? 1 : -1;
            int row = m_results->currentRow();
            for (int i = 0; i < rows; ++i) {
                row += step;
                if (row < 0) {
                    row = rows - 1;
                } else if (row >= rows) {
                    row = 0;
                }
                QListWidgetItem* candidate = m_results->item(row);
                if (candidate && candidate->data(kKindRole).toInt() != kKindHeader) {
                    m_results->setCurrentRow(row);
                    break;
                }
            }
            return true;   // never let the field's own cursor movement swallow the key
        }
        return false;
    }));

    // The palette is reused across invocations, so each showing has to reset itself: fresh query,
    // fresh results, focus in the field, centred over the shell.
    installEventFilter(new EventHook(this, [this](QEvent* event) {
        if (event->type() == QEvent::Show) {
            if (QWidget* owner = parentWidget()) {
                const QPoint centre = owner->mapToGlobal(owner->rect().center());
                move(centre.x() - width() / 2, centre.y() - height() / 2 - 40);
            }
            m_input->clear();
            m_input->setFocus(Qt::OtherFocusReason);
            onQueryChanged(QString());
        }
        return false;
    }));

    onQueryChanged(QString());
}

/// @oop-concept Polymorphic Result Handling :: four unrelated entity kinds share one ranked list;
/// each row carries its own kind in item data, so activation stays a single code path
void CommandPalette::onQueryChanged(const QString& q) {
    m_results->clear();
    const QString query = q.trimmed();

    // ---- source 1: navigation ------------------------------------------------------------
    std::vector<Hit> pages;
    for (std::size_t i = 0; i < kPages.size(); ++i) {
        const QString name = QString::fromUtf8(kPages[i]);
        const int score = fuzzyScore(name, query);
        if (score >= 0) {
            pages.push_back(Hit{score + 5, kKindPage, static_cast<int>(i), name,
                                QStringLiteral("Go to screen  ·  Ctrl+%1").arg(i + 1)});
        }
    }
    rank(pages, query.isEmpty() ? static_cast<int>(kPages.size()) : kPerSourceLimit);

    // ---- sources 2-4 are only worth querying once the user has typed something -------------
    std::vector<Hit> dishes;
    std::vector<Hit> people;
    std::vector<Hit> tickets;

    if (!query.isEmpty()) {
        for (const models::MenuItem& item : m_ctx.menu().search(query)) {
            const int score = std::max(fuzzyScore(item.name(), query),
                                       fuzzyScore(item.category(), query));
            if (score >= 0) {
                dishes.push_back(Hit{score, kKindMenuItem, item.id(), item.name(),
                                     QStringLiteral("%1  ·  %2")
                                         .arg(item.category(), core::formatNpr(item.price()))});
            }
        }
        rank(dishes, kPerSourceLimit);

        for (const models::Customer& customer : m_ctx.customers().search(query)) {
            const int score = std::max(fuzzyScore(customer.name(), query),
                                       fuzzyScore(customer.phone(), query));
            if (score >= 0) {
                people.push_back(Hit{score, kKindCustomer, customer.id(), customer.name(),
                                     QStringLiteral("%1  ·  %2 loyalty points")
                                         .arg(customer.phone().isEmpty()
                                                  ? QStringLiteral("no phone")
                                                  : customer.phone())
                                         .arg(customer.loyaltyPoints())});
            }
        }
        rank(people, kPerSourceLimit);

        for (const models::Order& order : m_ctx.orders().activeOrders()) {
            const int score = fuzzyScore(order.orderNumber(), query);
            if (score >= 0) {
                tickets.push_back(Hit{score, kKindOrder, order.id(), order.orderNumber(),
                                      QStringLiteral("%1  ·  %2  ·  %3")
                                          .arg(models::toString(order.type()),
                                               models::toString(order.status()),
                                               core::formatNpr(order.subtotal()))});
            }
        }
        rank(tickets, kPerSourceLimit);
    }

    // ---- render the merged list -------------------------------------------------------------
    const auto addSection = [this](const QString& caption, const std::vector<Hit>& hits) {
        if (hits.empty()) {
            return;
        }
        auto* header = new QListWidgetItem(caption.toUpper(), m_results);
        header->setData(kKindRole, kKindHeader);
        header->setFlags(Qt::NoItemFlags);

        for (const Hit& hit : hits) {
            auto* row = new QListWidgetItem(
                QStringLiteral("%1        %2").arg(hit.primary, hit.secondary), m_results);
            row->setData(kKindRole, hit.kind);
            row->setData(kIdRole, hit.id);
            row->setToolTip(hit.secondary);
        }
    };

    addSection(QStringLiteral("Navigate"), pages);
    addSection(QStringLiteral("Menu"), dishes);
    addSection(QStringLiteral("Customers"), people);
    addSection(QStringLiteral("Orders"), tickets);

    if (m_results->count() == 0) {
        auto* empty = new QListWidgetItem(
            QStringLiteral("No matches for “%1”").arg(query), m_results);
        empty->setData(kKindRole, kKindHeader);
        empty->setFlags(Qt::NoItemFlags);
        return;
    }

    // Preselect the first selectable row so Enter always does something sensible.
    for (int row = 0; row < m_results->count(); ++row) {
        if (m_results->item(row)->data(kKindRole).toInt() != kKindHeader) {
            m_results->setCurrentRow(row);
            break;
        }
    }
}

void CommandPalette::onActivated() {
    QListWidgetItem* item = m_results->currentItem();
    if (!item || item->data(kKindRole).toInt() == kKindHeader) {
        return;
    }

    const int kind = item->data(kKindRole).toInt();
    const int id = item->data(kIdRole).toInt();

    // The palette never acts: it names an intention and lets the shell carry it out.
    switch (kind) {
    case kKindPage:
        emit navigateRequested(id);
        break;
    case kKindMenuItem:
        emit navigateRequested(1);   // the Menu screen
        break;
    case kKindCustomer:
        emit navigateRequested(3);   // the Customers screen
        break;
    case kKindOrder:
        emit openOrderRequested(id);
        break;
    default:
        return;
    }
    accept();
}

} // namespace aluchop::gui

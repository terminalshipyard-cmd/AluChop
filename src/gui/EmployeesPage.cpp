/**
 * @file EmployeesPage.cpp
 * @brief Screen 5 — staff records, attendance and payroll preview.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The payroll table on this screen is the project's clearest piece of runtime polymorphism:
 * services::EmployeeService::staff() hands back
 * `std::vector<std::unique_ptr<models::Employee>>` holding Waiter, Chef, Manager and Admin
 * objects, and this file prints `roleName()` and `monthlyPay()` for each of them from a single
 * loop that never asks what concrete type it is holding. Four different pay formulas, no
 * `if (position == …)` anywhere.
 *
 * The "Loyalty" column makes the virtual-base diamond visible: a staff member whose phone number
 * also appears in the customer database is fused by
 * services::EmployeeService::staffCustomerFor() into a models::StaffCustomer — one identity that
 * is simultaneously on the payroll and in the loyalty programme.
 */

#include "aluchop/gui/EmployeesPage.hpp"

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include <QAbstractAnimation>
#include <QAbstractItemView>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSet>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTime>
#include <QTimeEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include "aluchop/core/Algorithms.hpp"
#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Money.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/gui/Widgets.hpp"
#include "aluchop/models/Admin.hpp"
#include "aluchop/models/Chef.hpp"
#include "aluchop/models/Customer.hpp"
#include "aluchop/models/Employee.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/Manager.hpp"
#include "aluchop/models/StaffCustomer.hpp"
#include "aluchop/models/Waiter.hpp"
#include "aluchop/services/AppContext.hpp"

namespace aluchop::gui {
namespace {

using core::Money;

// --- house measurements, identical across the four management screens ------------------------
constexpr int kPageMargin = 24;
constexpr int kPageSpacing = 18;
constexpr int kPanelPad = 18;
constexpr int kControlH = 38;
constexpr int kFadeMs = 220;
constexpr int kRowH = 44;  ///< ElegantTable's row height — panels are sized in whole rows

// --- staff table columns ---------------------------------------------------------------------
constexpr int kStaffName = 0;
constexpr int kStaffRole = 1;
constexpr int kStaffShift = 2;
constexpr int kStaffSalary = 3;
constexpr int kStaffRating = 4;
constexpr int kStaffLoyalty = 5;
constexpr int kStaffStatus = 6;

// --- payroll table columns --------------------------------------------------------------------
constexpr int kPayName = 0;
constexpr int kPayRole = 1;
constexpr int kPayRule = 2;
constexpr int kPayBase = 3;
constexpr int kPayExtras = 4;
constexpr int kPayTotal = 5;

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

/// @brief Keeps a destructive action red even when it is unavailable.
///
/// The shared sheet paints `#dangerButton:disabled` in the neutral border sage, so "Deactivate"
/// stopped reading as destructive the moment a non-manager signed in. A muted red says both
/// things at once: this is destructive, and right now it is not available.
void styleDestructive(QPushButton* button) {
    const QColor d = theme().danger;
    button->setStyleSheet(QStringLiteral("QPushButton:disabled {"
                                         " background-color: rgba(%1,%2,%3,12%);"
                                         " border: 1px solid rgba(%1,%2,%3,38%);"
                                         " color: rgba(%1,%2,%3,60%); }")
                              .arg(d.red())
                              .arg(d.green())
                              .arg(d.blue()));
}

/// @brief Table cell that sorts on a numeric key while displaying formatted text.
///
/// @oop-concept Method Overriding :: QTableWidgetItem::operator< is overridden so that
///              "Rs 45,000.00" sorts above "Rs 9,000.00" instead of sorting as text
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

/// @brief Gives a table a column policy that cannot starve its identity column.
///
/// `ElegantTable` turns on `stretchLastSection`, so "Status" hoarded the spare width at the right
/// edge while the `Stretch` name column was squeezed to the header minimum and printed
/// "Shashank Bh…". Bounded columns (a role, a shift, a salary, a rating) size to their content,
/// the name column takes what is left, and no section may shrink below @p minSection.
///
/// @param table          the table to configure.
/// @param stretchColumns the free-text columns that absorb the spare width.
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
/// `QHeaderView` starts with a descending sort indicator and `setSortingEnabled(true)` honours it,
/// which is why the roster opened on "Sita … Deepak". A staff list is read alphabetically.
void sortAscendingBy(QTableWidget* table, int column) {
    table->horizontalHeader()->setSortIndicator(column, Qt::AscendingOrder);
    table->sortItems(column, Qt::AscendingOrder);
}

/// @brief Repeats a cell's own text as its tooltip, so an elided cell is never lost data.
QTableWidgetItem* withTooltip(QTableWidgetItem* cell, const QString& detail = QString()) {
    cell->setToolTip(detail.isEmpty() ? cell->text() : detail);
    return cell;
}

// ---------------------------------------------------------------------------------------------
//  This month's variable pay — the inputs each role's monthlyPay() override consumes
// ---------------------------------------------------------------------------------------------

/// House figures for a month nobody has declared yet. They are *inputs*, never rules: what turns
/// an input into money is the role's own `monthlyPay()` override, and that lives in the model.
constexpr int kWaiterTipsPctOfBase = 12;    ///< pooled floor tips, as a share of base salary
constexpr int kManagerBonusPctOfBase = 10;  ///< the standing supervisory bonus
constexpr int kChefOvertimeHours = 14;      ///< overtime hours a kitchen shift typically runs up

/// @return the dynamic-property key holding one employee's declared extra for the month.
QByteArray extraKey(int employeeId) {
    return QByteArrayLiteral("aluchopMonthExtra_") + QByteArray::number(employeeId);
}

/// @brief What one role adds on top of base pay, in that role's own vocabulary.
struct RoleExtra {
    QString rule;    ///< short column text, e.g. "base + overtime"
    QString input;   ///< the input as this role measures it, e.g. "14 h × Rs 300.00"
    QString detail;  ///< tooltip: which override runs, and on what
    bool editable = false;  ///< false for a role that has no variable component at all
};

/// @brief Loads this month's declared input into @p person and describes what it will do.
///
/// The `employees` table deliberately stores no per-month variable pay (see
/// persistence::EmployeeRepository::update) — tips, overtime and bonuses are month figures, not
/// employment terms. So the payroll *preview* is where they are declared: the manager records
/// them per employee through "Extras…", and until they do, the house figures above stand in.
///
/// Nothing here computes pay. It writes an input through the concrete role's own validating
/// setter and then lets models::Employee::monthlyPay() resolve virtually — a Waiter adds its
/// tips, a Chef multiplies its hours by Chef::kOvertimeRatePerHour, a Manager adds its bonus,
/// and an Admin does whatever Manager does because it deliberately never overrode the rule.
///
/// @oop-concept Runtime Type Identification :: a role-specific *input* is the one thing a generic
///              Employee& cannot express, so dynamic_cast asks — and only to ask, never to branch
///              on the pay formula, which stays behind the virtual call
RoleExtra applyDeclaredExtra(const QObject* page, models::Employee& person) {
    const QVariant declared = page->property(extraKey(person.id()).constData());
    RoleExtra out;
    out.editable = true;

    if (auto* waiter = dynamic_cast<models::Waiter*>(&person)) {
        const Money tips = declared.isValid()
                               ? Money(declared.toLongLong())
                               : waiter->salary().percent(kWaiterTipsPctOfBase);
        waiter->addTip(tips);
        out.rule = QStringLiteral("base + tips");
        out.input = QStringLiteral("%1 pooled").arg(core::formatNpr(tips));
        out.detail = QStringLiteral("Waiter::monthlyPay() overrides Employee::monthlyPay() as "
                                    "salary() + tipsThisMonth().\nTips this month: %1.")
                         .arg(core::formatNpr(tips));
        return out;
    }
    if (auto* chef = dynamic_cast<models::Chef*>(&person)) {
        const int hours = declared.isValid() ? declared.toInt() : kChefOvertimeHours;
        chef->setOvertimeHours(hours);
        out.rule = QStringLiteral("base + overtime");
        out.input = QStringLiteral("%1 h × %2")
                        .arg(hours)
                        .arg(core::formatNpr(models::Chef::kOvertimeRatePerHour));
        out.detail = QStringLiteral("Chef::monthlyPay() overrides Employee::monthlyPay() as "
                                    "salary() + kOvertimeRatePerHour × overtimeHours().\n"
                                    "%1 hours at %2 an hour.")
                         .arg(hours)
                         .arg(core::formatNpr(models::Chef::kOvertimeRatePerHour));
        return out;
    }
    // Admin derives from Manager, so this branch catches both — which is the point: an Admin is
    // paid exactly as a Manager is, because Admin inherits the rule instead of copying it.
    if (auto* manager = dynamic_cast<models::Manager*>(&person)) {
        const Money bonus = declared.isValid()
                                ? Money(declared.toLongLong())
                                : manager->salary().percent(kManagerBonusPctOfBase);
        manager->setMonthlyBonus(bonus);
        const bool isAdmin = dynamic_cast<models::Admin*>(&person) != nullptr;
        out.rule = isAdmin ? QStringLiteral("base + bonus (inherited)")
                           : QStringLiteral("base + bonus");
        out.input = core::formatNpr(bonus);
        out.detail =
            isAdmin ? QStringLiteral("Admin never overrides monthlyPay(): it inherits "
                                     "Manager::monthlyPay(), salary() + monthlyBonus().\n"
                                     "Bonus this month: %1.")
                          .arg(core::formatNpr(bonus))
                    : QStringLiteral("Manager::monthlyPay() overrides Employee::monthlyPay() as "
                                     "salary() + monthlyBonus().\nBonus this month: %1.")
                          .arg(core::formatNpr(bonus));
        return out;
    }

    out.rule = QStringLiteral("base only");
    out.input = QStringLiteral("no variable component");
    out.detail = QStringLiteral("Employee::monthlyPay() — the base rule this hierarchy refines: "
                                "the salary and nothing else.");
    out.editable = false;
    return out;
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

void beginRepopulate(QTableWidget* table) {
    table->setSortingEnabled(false);
    table->clearContents();
    table->setRowCount(0);
}

void endRepopulate(QTableWidget* table) { table->setSortingEnabled(true); }

/// @brief Marks a screen as "repopulating" for as long as it is in scope.
///
/// Clearing and refilling a QTableWidget emits itemSelectionChanged several times. Without this
/// flag the selection handler would re-enter refresh() while refresh() was still running — a
/// genuine infinite recursion, not a theoretical one.
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

/// @return the four position tokens accepted by the schema's CHECK constraint.
QStringList positionTokens() {
    return QStringList{QStringLiteral("WAITER"), QStringLiteral("CHEF"),
                       QStringLiteral("MANAGER"), QStringLiteral("ADMIN")};
}

/// @return the three attendance tokens accepted by the schema's CHECK constraint.
QStringList attendanceTokens() {
    return QStringList{QStringLiteral("PRESENT"), QStringLiteral("ABSENT"),
                       QStringLiteral("LEAVE")};
}

/// @return the roster shifts offered by the picker (the column itself is free text).
QStringList shiftTokens() {
    return QStringList{QStringLiteral("DAY"), QStringLiteral("EVENING"), QStringLiteral("NIGHT"),
                       QStringLiteral("SPLIT")};
}

/// @return a five-glyph rating bar, e.g. `●●●●○` for 4.
QString ratingBar(int rating) {
    const int filled = core::clampValue(rating, 1, 5);
    QString bar;
    for (int i = 1; i <= 5; ++i) {
        bar += (i <= filled) ? QStringLiteral("●") : QStringLiteral("○");
    }
    return bar;
}

/// @brief Digits-only comparison key so "9779812…" and "977 9812 …" match.
QString phoneKey(const QString& raw) {
    QString digits;
    for (const QChar ch : raw) {
        if (ch.isDigit()) digits.append(ch);
    }
    return digits;
}

}  // namespace

// ============================================================================================
//  Construction
// ============================================================================================

EmployeesPage::EmployeesPage(services::AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    setObjectName(QStringLiteral("employeesPage"));

    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    page->setSpacing(kPageSpacing);

    // --- header band ------------------------------------------------------------------------
    auto* header = new QHBoxLayout;
    header->setSpacing(12);

    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    titles->addWidget(makeLabel(pageTitle(), QStringLiteral("pageTitle"), this, 24, QFont::Bold));
    auto* subtitle = makeLabel(QStringLiteral("Loading the roster…"),
                               QStringLiteral("mutedLabel"), this, 11);
    subtitle->setProperty("aluchopRole", QStringLiteral("employeesSubtitle"));
    titles->addWidget(subtitle);
    header->addLayout(titles);
    header->addStretch(1);

    auto* editBtn = makeButton(QStringLiteral("Edit"), QStringLiteral("ghostButton"), this);
    auto* deactivateBtn = makeButton(QStringLiteral("Deactivate"),
                                     QStringLiteral("dangerButton"), this);
    auto* hireBtn = makeButton(QStringLiteral("＋  Hire"), QStringLiteral("primaryButton"), this);
    editBtn->setProperty("aluchopRole", QStringLiteral("staffEdit"));
    deactivateBtn->setProperty("aluchopRole", QStringLiteral("staffDeactivate"));
    hireBtn->setProperty("aluchopRole", QStringLiteral("staffHire"));
    header->addWidget(editBtn);
    header->addWidget(deactivateBtn);
    header->addWidget(hireBtn);
    page->addLayout(header);

    // Read-only banner, shown only when the signed-in role is below Manager.
    auto* gateNote = makeLabel(QString(), QStringLiteral("mutedLabel"), this, 11);
    gateNote->setProperty("aluchopRole", QStringLiteral("staffGateNote"));
    gateNote->setVisible(false);
    page->addWidget(gateNote);

    // --- body -------------------------------------------------------------------------------
    auto* body = new QHBoxLayout;
    body->setSpacing(kPageSpacing);

    // Left column: staff list on top, payroll preview below --------------------------------
    auto* leftColumn = new QVBoxLayout;
    leftColumn->setSpacing(kPageSpacing);

    auto* staffPanel = new GlassPanel(this);
    auto* staffCol = new QVBoxLayout(staffPanel);
    staffCol->setContentsMargins(kPanelPad, kPanelPad, kPanelPad, kPanelPad);
    staffCol->setSpacing(12);

    auto* staffHead = new QHBoxLayout;
    staffHead->setSpacing(10);
    staffHead->addWidget(makeLabel(QStringLiteral("Staff"), QStringLiteral("sectionTitle"),
                                   staffPanel, 13, QFont::DemiBold));
    staffHead->addStretch(1);
    auto* diamondChip = makeChip(QStringLiteral("—"), staffPanel);
    diamondChip->setObjectName(QStringLiteral("staffDiamondChip"));
    diamondChip->setToolTip(QStringLiteral("Staff who are also enrolled in the loyalty programme "
                                           "— each of them is one StaffCustomer object."));
    staffHead->addWidget(diamondChip);
    auto* enrolBtn = makeButton(QStringLiteral("Enrol in loyalty"), QStringLiteral("ghostButton"),
                                staffPanel);
    enrolBtn->setProperty("aluchopRole", QStringLiteral("staffEnrol"));
    enrolBtn->setToolTip(QStringLiteral("Register the selected staff member in the loyalty "
                                        "programme under their own phone number. The two records "
                                        "then fuse into a single StaffCustomer identity."));
    staffHead->addWidget(enrolBtn);
    staffCol->addLayout(staffHead);

    // "Role" is the virtual roleName(); "Position" was the stored token that chooses it — the
    // same fact twice ("Chef" / "CHEF"). The token now rides on the Role cell's tooltip, and the
    // width it was wasting goes to the name.
    m_table = new ElegantTable(
        QStringList{QStringLiteral("Name"), QStringLiteral("Role"), QStringLiteral("Shift"),
                    QStringLiteral("Salary"), QStringLiteral("Rating"),
                    QStringLiteral("Loyalty"), QStringLiteral("Status")},
        staffPanel);
    configureColumns(m_table, {kStaffName});
    sortAscendingBy(m_table, kStaffName);
    m_table->setMinimumWidth(560);

    auto* staffEmpty = new EmptyState(
        QStringLiteral("Nobody on the roster"),
        QStringLiteral("Hire your first waiter, chef or manager to start tracking attendance and "
                       "payroll."),
        staffPanel);
    QPushButton* emptyHire = staffEmpty->enableAction(QStringLiteral("Hire the first employee"));

    auto* staffStack = new QStackedWidget(staffPanel);
    staffStack->setObjectName(QStringLiteral("employeesStaffStack"));
    staffStack->addWidget(m_table);
    staffStack->addWidget(staffEmpty);
    staffStack->setMinimumHeight(5 * kRowH);  // whole rows, never one sliced in half
    new WholeRowFitter(m_table, staffStack);
    staffCol->addWidget(staffStack, 1);
    // 1 : 1, because a seven-person roster needs seven staff rows *and* seven payroll rows. The
    // old 3 : 2 split showed the whole roster above a payroll table cut off after three names.
    leftColumn->addWidget(staffPanel, 1);

    auto* payrollPanel = new GlassPanel(this);
    auto* payrollCol = new QVBoxLayout(payrollPanel);
    payrollCol->setContentsMargins(kPanelPad, kPanelPad, kPanelPad, kPanelPad);
    payrollCol->setSpacing(12);

    auto* payrollHead = new QHBoxLayout;
    payrollHead->setSpacing(10);
    payrollHead->addWidget(makeLabel(QStringLiteral("Payroll preview"),
                                     QStringLiteral("sectionTitle"), payrollPanel, 13,
                                     QFont::DemiBold));
    payrollHead->addStretch(1);
    auto* payrollTotal = makeChip(QStringLiteral("—"), payrollPanel);
    payrollTotal->setObjectName(QStringLiteral("payrollTotalChip"));
    payrollHead->addWidget(payrollTotal);
    auto* extrasBtn = makeButton(QStringLiteral("Extras…"), QStringLiteral("ghostButton"),
                                 payrollPanel);
    extrasBtn->setProperty("aluchopRole", QStringLiteral("staffExtras"));
    extrasBtn->setToolTip(QStringLiteral("Declare this month's variable pay for the selected "
                                         "employee. Which field you are offered is decided by "
                                         "their role — tips, overtime hours or a bonus."));
    payrollHead->addWidget(extrasBtn);
    auto* payrollBtn = makeButton(QStringLiteral("Recompute"), QStringLiteral("ghostButton"),
                                  payrollPanel);
    payrollHead->addWidget(payrollBtn);
    payrollCol->addLayout(payrollHead);

    // Word-wrapped, because this sentence is longer than the panel and used to be sliced off
    // mid-word at "…one virtual monthlyPay(". A label that wraps costs one extra line of height
    // and keeps the sentence.
    auto* payrollBlurb =
        makeLabel(QStringLiteral("Base salary plus each role's own extra — tips for a waiter, "
                                 "overtime for a chef, a bonus for a manager, and nothing extra "
                                 "at all for a plain employee — every one of them resolved "
                                 "through the same virtual monthlyPay() call. The “Pay rule” "
                                 "column is the override that ran."),
                  QStringLiteral("mutedLabel"), payrollPanel, 11);
    payrollBlurb->setWordWrap(true);
    payrollBlurb->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    payrollCol->addWidget(payrollBlurb);

    m_payroll = new ElegantTable(
        QStringList{QStringLiteral("Name"), QStringLiteral("Role"), QStringLiteral("Pay rule"),
                    QStringLiteral("Base"), QStringLiteral("Extras"),
                    QStringLiteral("Monthly pay")},
        payrollPanel);
    m_payroll->verticalHeader()->setDefaultSectionSize(38);
    configureColumns(m_payroll, {kPayName});
    sortAscendingBy(m_payroll, kPayName);

    auto* payrollEmpty = new EmptyState(QStringLiteral("No active staff"),
                                        QStringLiteral("Payroll is computed from active "
                                                       "employees only."),
                                        payrollPanel);
    auto* payrollStack = new QStackedWidget(payrollPanel);
    payrollStack->setObjectName(QStringLiteral("employeesPayrollStack"));
    payrollStack->addWidget(m_payroll);
    payrollStack->addWidget(payrollEmpty);
    payrollStack->setMinimumHeight(5 * 38);
    new WholeRowFitter(m_payroll, payrollStack);
    payrollCol->addWidget(payrollStack, 1);
    leftColumn->addWidget(payrollPanel, 1);
    body->addLayout(leftColumn, 3);

    // Right column: attendance on top, the virtual-base diamond underneath ---------------------
    // Attendance alone left roughly two thirds of this column as blank card. The space now
    // carries the one relationship in this project a marker most needs to see.
    auto* rightColumn = new QVBoxLayout;
    rightColumn->setSpacing(kPageSpacing);

    auto* attendancePanel = new GlassPanel(this);
    attendancePanel->setMinimumWidth(340);
    auto* attendanceCol = new QVBoxLayout(attendancePanel);
    attendanceCol->setContentsMargins(kPanelPad, kPanelPad, kPanelPad, kPanelPad);
    attendanceCol->setSpacing(12);
    attendanceCol->addWidget(makeLabel(QStringLiteral("Attendance"),
                                       QStringLiteral("sectionTitle"), attendancePanel, 13,
                                       QFont::DemiBold));

    auto* whoLabel = makeLabel(QStringLiteral("No staff member selected"), QString(),
                               attendancePanel, 15, QFont::DemiBold);
    whoLabel->setObjectName(QStringLiteral("attendanceWho"));
    whoLabel->setWordWrap(true);
    attendanceCol->addWidget(whoLabel);

    auto* monthRow = new QHBoxLayout;
    monthRow->setSpacing(8);
    auto* month = new QDateEdit(QDate::currentDate(), attendancePanel);
    month->setObjectName(QStringLiteral("attendanceMonth"));
    month->setDisplayFormat(QStringLiteral("MMMM yyyy"));
    month->setCalendarPopup(true);
    month->setMinimumHeight(kControlH);
    monthRow->addWidget(month, 1);
    auto* markBtn = makeButton(QStringLiteral("Mark day"), QStringLiteral("ghostButton"),
                               attendancePanel);
    markBtn->setProperty("aluchopRole", QStringLiteral("staffMark"));
    monthRow->addWidget(markBtn);
    attendanceCol->addLayout(monthRow);

    auto* summaryRow = new QHBoxLayout;
    summaryRow->setSpacing(8);
    auto* presentChip = makeChip(QStringLiteral("0 present"), attendancePanel);
    presentChip->setObjectName(QStringLiteral("attendancePresentChip"));
    auto* absentChip = makeChip(QStringLiteral("0 absent"), attendancePanel);
    absentChip->setObjectName(QStringLiteral("attendanceAbsentChip"));
    auto* leaveChip = makeChip(QStringLiteral("0 leave"), attendancePanel);
    leaveChip->setObjectName(QStringLiteral("attendanceLeaveChip"));
    summaryRow->addWidget(presentChip);
    summaryRow->addWidget(absentChip);
    summaryRow->addWidget(leaveChip);
    summaryRow->addStretch(1);
    attendanceCol->addLayout(summaryRow);

    m_attendance = new ElegantTable(
        QStringList{QStringLiteral("Date"), QStringLiteral("Status"), QStringLiteral("In"),
                    QStringLiteral("Out")},
        attendancePanel);
    m_attendance->verticalHeader()->setDefaultSectionSize(38);
    configureColumns(m_attendance, {0});

    auto* attendanceEmpty =
        new EmptyState(QStringLiteral("Nothing recorded"),
                       QStringLiteral("Pick a staff member and a month, then mark the days."),
                       attendancePanel);
    auto* attendanceStack = new QStackedWidget(attendancePanel);
    attendanceStack->setObjectName(QStringLiteral("employeesAttendanceStack"));
    attendanceStack->addWidget(m_attendance);
    attendanceStack->addWidget(attendanceEmpty);
    attendanceStack->setMinimumHeight(4 * kRowH);
    new WholeRowFitter(m_attendance, attendanceStack);
    attendanceCol->addWidget(attendanceStack, 1);
    rightColumn->addWidget(attendancePanel, 3);

    // --- the virtual-base diamond, given its own panel -----------------------------------------
    // models::StaffCustomer is the single most important inheritance fact in this project: one
    // object that is simultaneously on the payroll and in the loyalty programme, possible only
    // because Employee and Customer both derive Person *virtually*. It used to be visible as one
    // number in one table cell. It now says what it is.
    auto* diamondPanel = new GlassPanel(this);
    auto* diamondCol = new QVBoxLayout(diamondPanel);
    diamondCol->setContentsMargins(kPanelPad, kPanelPad, kPanelPad, kPanelPad);
    diamondCol->setSpacing(10);

    auto* diamondHead = new QHBoxLayout;
    diamondHead->setSpacing(10);
    diamondHead->addWidget(makeLabel(QStringLiteral("◆  Staff-customers"),
                                     QStringLiteral("sectionTitle"), diamondPanel, 13,
                                     QFont::DemiBold));
    diamondHead->addStretch(1);
    auto* diamondCount = makeChip(QStringLiteral("—"), diamondPanel);
    diamondCount->setObjectName(QStringLiteral("staffDiamondCountChip"));
    diamondHead->addWidget(diamondCount);
    diamondCol->addLayout(diamondHead);

    auto* diamondNote =
        makeLabel(QStringLiteral("One person, one identity, two books. Employee and Customer both "
                                 "inherit Person as a virtual base, so a colleague who eats here "
                                 "is a single StaffCustomer object — one id, one name, one phone "
                                 "— carrying a salary and a loyalty balance at the same time."),
                  QStringLiteral("mutedLabel"), diamondPanel, 11);
    diamondNote->setWordWrap(true);
    diamondCol->addWidget(diamondNote);

    auto* diamondTable = new ElegantTable(
        QStringList{QStringLiteral("Staff-customer"), QStringLiteral("Loyalty"),
                    QStringLiteral("Till discount")},
        diamondPanel);
    diamondTable->setObjectName(QStringLiteral("staffDiamondTable"));
    diamondTable->setSortingEnabled(false);
    diamondTable->verticalHeader()->setDefaultSectionSize(38);
    configureColumns(diamondTable, {0});

    auto* diamondEmpty =
        new EmptyState(QStringLiteral("Nobody is on both books yet"),
                       QStringLiteral("Pick a colleague in the staff list and press “Enrol in "
                                      "loyalty”. The moment their phone number is also a "
                                      "customer's, the two rows fuse into one StaffCustomer."),
                       diamondPanel);
    auto* diamondStack = new QStackedWidget(diamondPanel);
    diamondStack->setObjectName(QStringLiteral("employeesDiamondStack"));
    diamondStack->addWidget(diamondTable);
    diamondStack->addWidget(diamondEmpty);
    diamondStack->setMinimumHeight(3 * 38);
    new WholeRowFitter(diamondTable, diamondStack);
    diamondCol->addWidget(diamondStack, 1);
    rightColumn->addWidget(diamondPanel, 2);

    body->addLayout(rightColumn, 2);
    page->addLayout(body, 1);

    // --- wiring -----------------------------------------------------------------------------
    connect(hireBtn, &QPushButton::clicked, this, &EmployeesPage::onHire);
    connect(emptyHire, &QPushButton::clicked, this, &EmployeesPage::onHire);
    connect(editBtn, &QPushButton::clicked, this, &EmployeesPage::onEdit);
    connect(m_table, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*) { onEdit(); });
    connect(deactivateBtn, &QPushButton::clicked, this, &EmployeesPage::onDeactivate);

    // Enrolling a staff member in the loyalty programme is what actually *creates* a
    // models::StaffCustomer: EmployeeService::staffCustomerFor() fuses the two rows the moment a
    // customer shares an active employee's phone number. Without this action the diamond can only
    // be demonstrated by hand-editing the database, and the shipped seed has nobody enrolled.
    connect(enrolBtn, &QPushButton::clicked, this, [this]() {
        const int id = selectedEmployeeId();
        if (id == 0) {
            m_ctx.notifications().notify(QStringLiteral("Nobody selected"),
                                         QStringLiteral("Choose a staff member first."), 2);
            return;
        }
        const std::optional<models::Employee> person = m_ctx.employees().byId(id);
        if (!person) {
            refresh();
            return;
        }
        if (person->phone().trimmed().isEmpty()) {
            m_ctx.notifications().notify(
                QStringLiteral("No phone number"),
                QStringLiteral("The phone number is the key the two records are fused on — give "
                               "%1 one first.").arg(person->name()),
                2);
            return;
        }
        if (const auto existing = m_ctx.customers().byPhone(person->phone())) {
            m_ctx.notifications().notify(
                QStringLiteral("Already enrolled"),
                QStringLiteral("%1 is already in the loyalty programme with %2 points.")
                    .arg(existing->name())
                    .arg(existing->loyaltyPoints()),
                2);
            refresh();
            return;
        }

        QMessageBox confirm(this);
        confirm.setWindowTitle(QStringLiteral("Enrol in loyalty"));
        confirm.setIcon(QMessageBox::Question);
        confirm.setText(QStringLiteral("Enrol %1 in the loyalty programme?").arg(person->name()));
        confirm.setInformativeText(
            QStringLiteral("They keep one identity: the same name and the same phone number now "
                           "carry both a payroll record and a loyalty balance, which is exactly "
                           "what a StaffCustomer is. Bills raised against that phone number then "
                           "qualify for the staff discount."));
        confirm.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
        confirm.setDefaultButton(QMessageBox::Yes);
        confirm.button(QMessageBox::Yes)->setText(QStringLiteral("Enrol"));
        confirm.button(QMessageBox::Yes)->setObjectName(QStringLiteral("primaryButton"));
        confirm.button(QMessageBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
        if (confirm.exec() != QMessageBox::Yes) return;

        const auto created = m_ctx.customers().create(person->name(), person->phone(),
                                                      person->email());
        if (created.isOk()) {
            m_ctx.notifications().notify(
                QStringLiteral("Enrolled"),
                QStringLiteral("%1 is now a staff-customer — one identity on both books.")
                    .arg(person->name()),
                1);
            refresh();
        } else {
            m_ctx.notifications().notify(QStringLiteral("Could not enrol"), created.error(), 3);
        }
    });
    // "Extras…" is where the month's variable pay is declared. Which single field the dialog
    // offers is decided by the concrete role, which is the same fact the payroll table's "Pay
    // rule" column states — a waiter is asked for tips, a chef for hours, a manager for a bonus,
    // and an employee with no override is told it has none.
    connect(extrasBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx.auth().hasRole(models::UserRole::Manager)) {
            m_ctx.notifications().notify(QStringLiteral("Not allowed"),
                                         QStringLiteral("Declaring payroll extras requires "
                                                        "manager access."),
                                         3);
            return;
        }
        const int id = selectedEmployeeId();
        if (id == 0) {
            m_ctx.notifications().notify(QStringLiteral("Nobody selected"),
                                         QStringLiteral("Choose a staff member first."), 2);
            return;
        }
        // The typed roster, not the base-sliced byId(): only the concrete object knows which
        // input its own monthlyPay() override consumes.
        std::vector<std::unique_ptr<models::Employee>> roster = m_ctx.employees().staff();
        models::Employee* person = nullptr;
        for (const std::unique_ptr<models::Employee>& candidate : roster) {
            if (candidate && candidate->id() == id) {
                person = candidate.get();
                break;
            }
        }
        if (!person) {
            refresh();
            return;
        }

        const bool isChef = dynamic_cast<models::Chef*>(person) != nullptr;
        const bool isWaiter = dynamic_cast<models::Waiter*>(person) != nullptr;
        const bool isManager = dynamic_cast<models::Manager*>(person) != nullptr;
        if (!isChef && !isWaiter && !isManager) {
            m_ctx.notifications().notify(
                QStringLiteral("No variable pay"),
                QStringLiteral("%1's role does not override monthlyPay(), so there is nothing to "
                               "add on top of the base salary.").arg(person->name()),
                2);
            return;
        }

        const QVariant declared = property(extraKey(id).constData());

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("This month's extra"));
        dialog.setMinimumWidth(420);
        auto* column = new QVBoxLayout(&dialog);
        column->setContentsMargins(22, 22, 22, 22);
        column->setSpacing(14);
        column->addWidget(makeLabel(QStringLiteral("%1 · %2")
                                        .arg(person->name(), person->roleName()),
                                    QStringLiteral("sectionTitle"), &dialog, 16, QFont::DemiBold));
        auto* why = makeLabel(
            isChef ? QStringLiteral("Chef::monthlyPay() adds overtime hours at the house rate of "
                                    "%1 an hour. Hours are a month figure, so they are declared "
                                    "here rather than stored on the employment record.")
                         .arg(core::formatNpr(models::Chef::kOvertimeRatePerHour))
                   : isWaiter
                         ? QStringLiteral("Waiter::monthlyPay() adds the tips collected this "
                                          "month on top of the base salary.")
                         : QStringLiteral("Manager::monthlyPay() adds a fixed monthly bonus on "
                                          "top of the base salary."),
            QStringLiteral("mutedLabel"), &dialog, 11);
        why->setWordWrap(true);
        column->addWidget(why);

        auto* form = new QFormLayout;
        form->setSpacing(10);
        auto* amount = new QSpinBox(&dialog);
        amount->setMinimumHeight(kControlH);
        if (isChef) {
            amount->setRange(0, 400);
            amount->setSuffix(QStringLiteral(" h"));
            amount->setValue(declared.isValid() ? declared.toInt() : kChefOvertimeHours);
            form->addRow(QStringLiteral("Overtime"), amount);
        } else {
            const Money fallback =
                isWaiter ? person->salary().percent(kWaiterTipsPctOfBase)
                         : person->salary().percent(kManagerBonusPctOfBase);
            amount->setRange(0, 10000000);
            amount->setSingleStep(500);
            amount->setPrefix(QStringLiteral("Rs "));
            amount->setGroupSeparatorShown(true);
            amount->setValue(static_cast<int>(
                declared.isValid() ? Money(declared.toLongLong()).wholeRupees()
                                   : fallback.wholeRupees()));
            form->addRow(isWaiter ? QStringLiteral("Tips") : QStringLiteral("Bonus"), amount);
        }
        column->addLayout(form);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                                 QDialogButtonBox::RestoreDefaults,
                                             &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Apply"));
        buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primaryButton"));
        buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
        buttons->button(QDialogButtonBox::RestoreDefaults)
            ->setText(QStringLiteral("House figure"));
        buttons->button(QDialogButtonBox::RestoreDefaults)
            ->setObjectName(QStringLiteral("ghostButton"));
        column->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        // "House figure" clears the declaration so the default stands in again.
        connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog,
                [&dialog]() { dialog.done(QDialog::Accepted + 1); });

        const int verdict = dialog.exec();
        if (verdict == QDialog::Rejected) return;
        if (verdict == QDialog::Accepted + 1) {
            setProperty(extraKey(id).constData(), QVariant());  // back to the house figure
        } else {
            setProperty(extraKey(id).constData(),
                        isChef ? QVariant(static_cast<qlonglong>(amount->value()))
                               : QVariant(static_cast<qlonglong>(
                                     Money::fromRupees(amount->value()).paisa())));
        }
        refresh();
    });
    connect(markBtn, &QPushButton::clicked, this, &EmployeesPage::onMarkAttendance);
    connect(payrollBtn, &QPushButton::clicked, this, &EmployeesPage::onShowPayroll);
    connect(month, &QDateEdit::dateChanged, this, [this](const QDate&) {
        if (BusyGuard::busy(this)) return;
        refresh();
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (BusyGuard::busy(this)) return;  // ignore the churn a repopulation causes
        refresh();
    });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &EmployeesPage::refresh);

    new EntranceAnimator(this);
}

// ============================================================================================
//  Page interface
// ============================================================================================

QString EmployeesPage::pageTitle() const { return QStringLiteral("Employees"); }

void EmployeesPage::refresh() {
    // Must be idempotent and must never throw — Page's contract.
    if (BusyGuard::busy(this)) return;
    BusyGuard guard(this);
    try {
        const Palette& p = theme();

        // Role gate: below Manager the screen becomes a read-only roster.
        const bool mayManage = m_ctx.auth().hasRole(models::UserRole::Manager);
        for (QPushButton* button : findChildren<QPushButton*>()) {
            const QString role = button->property("aluchopRole").toString();
            if (role == QLatin1String("staffHire") || role == QLatin1String("staffEdit") ||
                role == QLatin1String("staffDeactivate") || role == QLatin1String("staffMark") ||
                role == QLatin1String("staffExtras")) {
                button->setEnabled(mayManage);
            }
            if (role == QLatin1String("staffDeactivate")) {
                styleDestructive(button);  // re-read the palette: Light/Dark switch live
            }
        }
        for (QLabel* label : findChildren<QLabel*>()) {
            if (label->property("aluchopRole").toString() == QLatin1String("staffGateNote")) {
                label->setVisible(!mayManage);
                label->setText(QStringLiteral("Read-only — hiring, edits and attendance need "
                                              "manager access."));
                label->setStyleSheet(QStringLiteral("color: %1;").arg(p.danger.name()));
            }
        }

        const int previousId = selectedEmployeeId();

        // --- the polymorphic staff collection -------------------------------------------------
        // @oop-concept Runtime Polymorphism :: every cell below is produced through Employee*
        const std::vector<std::unique_ptr<models::Employee>> roster = m_ctx.employees().staff();

        // --- the diamond, made visible --------------------------------------------------------
        // Only employees whose phone is also a customer phone can fuse, so the phone set is built
        // first and the (more expensive) fusion is asked for only where it can succeed.
        QSet<QString> staffPhones;
        for (const std::unique_ptr<models::Employee>& person : roster) {
            if (person && !person->phone().isEmpty()) staffPhones.insert(phoneKey(person->phone()));
        }
        QHash<QString, QString> loyaltyByPhone;  // phone key -> "◆  340 pts"
        QHash<QString, QString> fusedNote;       // phone key -> what that fusion means
        std::vector<models::StaffCustomer> fusedPeople;  // the diamond panel's own list
        if (!staffPhones.isEmpty()) {
            for (const models::Customer& guest : m_ctx.customers().all()) {
                const QString key = phoneKey(guest.phone());
                if (key.isEmpty() || !staffPhones.contains(key)) continue;
                const std::optional<models::StaffCustomer> fused =
                    m_ctx.employees().staffCustomerFor(guest.id());
                if (!fused) continue;
                fusedPeople.push_back(*fused);
                // One object, one identity: name() comes from the shared virtual Person base,
                // loyaltyPoints() from the Customer branch, staffDiscountPercent() is its own.
                loyaltyByPhone.insert(key, QStringLiteral("◆  %1 pts")
                                               .arg(fused->loyaltyPoints()));
                fusedNote.insert(key,
                                 QStringLiteral("%1\n\nOn the payroll AND in the loyalty "
                                                "programme: one StaffCustomer object with a "
                                                "single shared identity, %2 loyalty points, and "
                                                "%3%% off at the till.")
                                     .arg(fused->displayLabel())
                                     .arg(fused->loyaltyPoints())
                                     .arg(fused->staffDiscountPercent()));
            }
        }

        // --- staff table ----------------------------------------------------------------------
        beginRepopulate(m_table);
        int rowIndex = 0;
        int activeCount = 0;
        int fusedCount = 0;
        for (const std::unique_ptr<models::Employee>& person : roster) {
            if (!person) continue;
            m_table->insertRow(rowIndex);
            if (person->isActive()) ++activeCount;

            auto* nameCell = textCell(person->name());
            nameCell->setData(Qt::UserRole, person->id());
            QFont nameFont = nameCell->font();
            nameFont.setWeight(QFont::DemiBold);
            nameCell->setFont(nameFont);
            m_table->setItem(rowIndex, kStaffName,
                             withTooltip(nameCell, QStringLiteral("%1\n%2\n%3")
                                                       .arg(person->displayLabel(),
                                                            person->phone().isEmpty()
                                                                ? QStringLiteral("no phone on "
                                                                                 "file")
                                                                : person->phone(),
                                                            person->email())));

            // roleName() is virtual: Waiter/Chef/Manager/Admin each answer for themselves. The
            // stored position token (the thing that chose the subclass) is the tooltip, not a
            // second column repeating the same word in capitals.
            auto* roleCell = textCell(person->roleName());
            roleCell->setForeground(p.primary);
            roleCell->setToolTip(QStringLiteral("%1\nStored position token “%2” — that token is "
                                                "what chooses the concrete C++ subclass.")
                                     .arg(person->displayLabel(), person->position()));
            m_table->setItem(rowIndex, kStaffRole, roleCell);

            m_table->setItem(rowIndex, kStaffShift, textCell(person->shift()));

            const Money salary = person->salary();
            m_table->setItem(rowIndex, kStaffSalary,
                             numberCell(core::formatNpr(salary),
                                        static_cast<double>(salary.paisa())));

            auto* ratingCell = new SortableCell(ratingBar(person->performanceRating()),
                                                person->performanceRating());
            ratingCell->setTextAlignment(Qt::AlignCenter);
            ratingCell->setForeground(person->performanceRating() >= 4 ? p.success
                                                                       : p.textMuted);
            ratingCell->setToolTip(QStringLiteral("Performance rating %1 of 5")
                                       .arg(person->performanceRating()));
            m_table->setItem(rowIndex, kStaffRating, ratingCell);

            // The diamond, one row at a time: ◆ marks a staff member who is simultaneously a
            // loyalty customer, and the number beside it is that person's real points balance
            // read through the Customer branch of the fused StaffCustomer object.
            const QString loyalty = loyaltyByPhone.value(phoneKey(person->phone()));
            auto* loyaltyCell = textCell(loyalty.isEmpty() ? QStringLiteral("Not enrolled")
                                                           : loyalty);
            if (!loyalty.isEmpty()) {
                ++fusedCount;
                // A badge, not just a coloured number: this cell is the only place in the roster
                // where a row is simultaneously an Employee and a Customer, and it should be
                // impossible to scan past.
                QColor tint = p.accent;
                tint.setAlpha(70);
                loyaltyCell->setBackground(QBrush(tint));
                loyaltyCell->setForeground(p.primary);
                loyaltyCell->setTextAlignment(Qt::AlignCenter);
                QFont loyaltyFont = loyaltyCell->font();
                loyaltyFont.setWeight(QFont::Bold);
                loyaltyCell->setFont(loyaltyFont);
                loyaltyCell->setToolTip(fusedNote.value(phoneKey(person->phone())));
            } else {
                loyaltyCell->setForeground(p.textMuted);
                loyaltyCell->setToolTip(
                    QStringLiteral("%1 is not in the loyalty programme. Select the row and press "
                                   "“Enrol in loyalty” to fuse the two records into one "
                                   "StaffCustomer.")
                        .arg(person->name()));
            }
            m_table->setItem(rowIndex, kStaffLoyalty, loyaltyCell);

            auto* statusCell = textCell(person->isActive() ? QStringLiteral("Active")
                                                           : QStringLiteral("Inactive"));
            statusCell->setForeground(person->isActive() ? p.success : p.danger);
            m_table->setItem(rowIndex, kStaffStatus, statusCell);

            ++rowIndex;
        }
        endRepopulate(m_table);

        if (auto* stack = findChild<QStackedWidget*>(QStringLiteral("employeesStaffStack"))) {
            stack->setCurrentIndex(rowIndex == 0 ? 1 : 0);
        }

        if (previousId != 0) {
            for (int row = 0; row < m_table->rowCount(); ++row) {
                const QTableWidgetItem* cell = m_table->item(row, 0);
                if (cell && cell->data(Qt::UserRole).toInt() == previousId) {
                    m_table->selectRow(row);
                    break;
                }
            }
        }

        for (QLabel* label : findChildren<QLabel*>()) {
            if (label->property("aluchopRole").toString() == QLatin1String("employeesSubtitle")) {
                QString text = QStringLiteral("%1 on the roster  ·  %2 active")
                                   .arg(rowIndex)
                                   .arg(activeCount);
                if (fusedCount > 0) {
                    text += QStringLiteral("  ·  %1 also in the loyalty programme")
                                .arg(fusedCount);
                }
                label->setText(text);
            }
        }

        // The diamond's own headline: how many staff are simultaneously loyalty customers.
        if (auto* chip = findChild<QLabel*>(QStringLiteral("staffDiamondChip"))) {
            chip->setText(fusedCount == 0
                              ? QStringLiteral("no staff enrolled")
                              : QStringLiteral("◆  %1 staff-customer%2")
                                    .arg(fusedCount)
                                    .arg(fusedCount == 1 ? QString() : QStringLiteral("s")));
            styleChip(chip, fusedCount > 0 ? p.secondary : p.textMuted);
        }

        onShowPayroll();

        // --- attendance for the selected employee and month -----------------------------------
        auto* monthEdit = findChild<QDateEdit*>(QStringLiteral("attendanceMonth"));
        auto* whoLabel = findChild<QLabel*>(QStringLiteral("attendanceWho"));
        auto* attendanceStack =
            findChild<QStackedWidget*>(QStringLiteral("employeesAttendanceStack"));
        auto* presentChip = findChild<QLabel*>(QStringLiteral("attendancePresentChip"));
        auto* absentChip = findChild<QLabel*>(QStringLiteral("attendanceAbsentChip"));
        auto* leaveChip = findChild<QLabel*>(QStringLiteral("attendanceLeaveChip"));

        const int employeeId = selectedEmployeeId();
        beginRepopulate(m_attendance);  // stays unsorted: a month reads best chronologically

        int present = 0;
        int absent = 0;
        int leave = 0;

        if (employeeId == 0 || !monthEdit) {
            if (whoLabel) whoLabel->setText(QStringLiteral("No staff member selected"));
            if (attendanceStack) {
                attendanceStack->setCurrentIndex(1);
                if (auto* empty = dynamic_cast<EmptyState*>(attendanceStack->widget(1))) {
                    empty->setText(QStringLiteral("Nothing recorded"),
                                   QStringLiteral("Select somebody in the staff list to see "
                                                  "their month."));
                }
            }
        } else {
            const std::optional<models::Employee> selected = m_ctx.employees().byId(employeeId);
            if (whoLabel) {
                whoLabel->setText(selected ? QStringLiteral("%1 · %2")
                                                 .arg(selected->name(), selected->roleName())
                                           : QStringLiteral("Staff member"));
            }
            const QDate monthDate = monthEdit->date();
            const auto rows = m_ctx.employees().attendanceFor(employeeId, monthDate.year(),
                                                              monthDate.month());
            int attendanceRow = 0;
            for (const auto& entry : rows) {
                const QDate day = std::get<0>(entry);
                const QString status = std::get<1>(entry);
                const QTime checkIn = std::get<2>(entry);
                const QTime checkOut = std::get<3>(entry);

                m_attendance->insertRow(attendanceRow);
                m_attendance->setItem(
                    attendanceRow, 0,
                    textCell(day.toString(QStringLiteral("ddd dd MMM"))));

                auto* statusCell = textCell(status);
                if (status == QLatin1String("PRESENT")) {
                    ++present;
                    statusCell->setForeground(p.success);
                } else if (status == QLatin1String("ABSENT")) {
                    ++absent;
                    statusCell->setForeground(p.danger);
                } else {
                    ++leave;
                    statusCell->setForeground(p.secondary);
                }
                m_attendance->setItem(attendanceRow, 1, statusCell);

                m_attendance->setItem(attendanceRow, 2,
                                      textCell(checkIn.isValid()
                                                   ? checkIn.toString(QStringLiteral("HH:mm"))
                                                   : QStringLiteral("—"),
                                               Qt::AlignCenter));
                m_attendance->setItem(attendanceRow, 3,
                                      textCell(checkOut.isValid()
                                                   ? checkOut.toString(QStringLiteral("HH:mm"))
                                                   : QStringLiteral("—"),
                                               Qt::AlignCenter));
                ++attendanceRow;
            }

            if (attendanceStack) {
                attendanceStack->setCurrentIndex(attendanceRow == 0 ? 1 : 0);
                if (auto* empty = dynamic_cast<EmptyState*>(attendanceStack->widget(1))) {
                    empty->setText(QStringLiteral("No days marked in %1")
                                       .arg(monthDate.toString(QStringLiteral("MMMM yyyy"))),
                                   QStringLiteral("Use “Mark day” to record presence, absence "
                                                  "or leave."));
                }
            }
        }

        if (presentChip) {
            presentChip->setText(QStringLiteral("%1 present").arg(present));
            styleChip(presentChip, present > 0 ? p.success : p.textMuted);
        }
        if (absentChip) {
            absentChip->setText(QStringLiteral("%1 absent").arg(absent));
            styleChip(absentChip, absent > 0 ? p.danger : p.textMuted);
        }
        if (leaveChip) {
            leaveChip->setText(QStringLiteral("%1 leave").arg(leave));
            styleChip(leaveChip, leave > 0 ? p.secondary : p.textMuted);
        }
    } catch (const std::exception& e) {
        m_ctx.notifications().notify(QStringLiteral("Employees"), QString::fromUtf8(e.what()), 3);
    }
}

// ============================================================================================
//  Slots
// ============================================================================================

void EmployeesPage::onHire() {
    if (!m_ctx.auth().hasRole(models::UserRole::Manager)) {
        m_ctx.notifications().notify(QStringLiteral("Not allowed"),
                                     QStringLiteral("Hiring requires manager access."), 3);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Hire staff"));
    dialog.setMinimumWidth(460);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(22, 22, 22, 22);
    column->setSpacing(16);
    column->addWidget(makeLabel(QStringLiteral("Hire a staff member"),
                                QStringLiteral("sectionTitle"), &dialog, 16, QFont::DemiBold));
    column->addWidget(makeLabel(QStringLiteral("The position decides which concrete class the "
                                               "service allocates — Waiter, Chef, Manager or "
                                               "Admin — and therefore how their pay is computed."),
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
    auto* position = new QComboBox(&dialog);
    position->addItems(positionTokens());
    position->setMinimumHeight(kControlH);
    auto* salary = new QSpinBox(&dialog);
    salary->setRange(0, 100000000);
    salary->setSingleStep(1000);
    salary->setValue(25000);
    salary->setPrefix(QStringLiteral("Rs "));
    salary->setGroupSeparatorShown(true);
    auto* shift = new QComboBox(&dialog);
    shift->setEditable(true);
    shift->addItems(shiftTokens());
    shift->setMinimumHeight(kControlH);

    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Phone"), phone);
    form->addRow(QStringLiteral("E-mail"), email);
    form->addRow(QStringLiteral("Position"), position);
    form->addRow(QStringLiteral("Monthly salary"), salary);
    form->addRow(QStringLiteral("Shift"), shift);
    column->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Hire"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primaryButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    // Salary is entered in whole rupees and converted to paisa immediately — no double ever
    // holds currency in this application.
    const Money monthly = Money::fromRupees(salary->value());
    const auto hired = m_ctx.employees().hire(name->text().trimmed(), phone->text().trimmed(),
                                              email->text().trimmed(), position->currentText(),
                                              monthly, shift->currentText().trimmed());
    if (hired.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Staff hired"),
                                     QStringLiteral("%1 joined as a %2.")
                                         .arg(name->text().trimmed(),
                                              position->currentText().toLower()),
                                     1);
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not hire"), hired.error(), 3);
    }
}

void EmployeesPage::onEdit() {
    if (!m_ctx.auth().hasRole(models::UserRole::Manager)) {
        m_ctx.notifications().notify(QStringLiteral("Not allowed"),
                                     QStringLiteral("Editing staff requires manager access."), 3);
        return;
    }
    const int id = selectedEmployeeId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Nobody selected"),
                                     QStringLiteral("Choose a staff member first."), 2);
        return;
    }
    const std::optional<models::Employee> existing = m_ctx.employees().byId(id);
    if (!existing) {
        m_ctx.notifications().notify(QStringLiteral("Record missing"),
                                     QStringLiteral("That staff record is no longer present."), 3);
        refresh();
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit staff"));
    dialog.setMinimumWidth(460);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(22, 22, 22, 22);
    column->setSpacing(16);
    column->addWidget(makeLabel(QStringLiteral("Edit %1").arg(existing->name()),
                                QStringLiteral("sectionTitle"), &dialog, 16, QFont::DemiBold));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    auto* name = new QLineEdit(existing->name(), &dialog);
    name->setMinimumHeight(kControlH);
    auto* phone = new QLineEdit(existing->phone(), &dialog);
    phone->setMinimumHeight(kControlH);
    auto* email = new QLineEdit(existing->email(), &dialog);
    email->setMinimumHeight(kControlH);
    auto* position = new QComboBox(&dialog);
    position->addItems(positionTokens());
    position->setCurrentText(existing->position());
    position->setMinimumHeight(kControlH);
    auto* salary = new QSpinBox(&dialog);
    salary->setRange(0, 100000000);
    salary->setSingleStep(1000);
    salary->setValue(static_cast<int>(existing->salary().wholeRupees()));
    salary->setPrefix(QStringLiteral("Rs "));
    salary->setGroupSeparatorShown(true);
    auto* shift = new QComboBox(&dialog);
    shift->setEditable(true);
    shift->addItems(shiftTokens());
    shift->setCurrentText(existing->shift());
    shift->setMinimumHeight(kControlH);
    auto* rating = new QSpinBox(&dialog);
    rating->setRange(1, 5);
    rating->setValue(existing->performanceRating());
    rating->setSuffix(QStringLiteral(" / 5"));
    auto* hired = new QDateEdit(existing->hiredDate().isValid() ? existing->hiredDate()
                                                                : QDate::currentDate(),
                                &dialog);
    hired->setCalendarPopup(true);
    hired->setDisplayFormat(QStringLiteral("dd MMM yyyy"));
    hired->setMinimumHeight(kControlH);
    auto* active = new QCheckBox(QStringLiteral("Currently employed"), &dialog);
    active->setChecked(existing->isActive());
    active->setCursor(Qt::PointingHandCursor);

    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Phone"), phone);
    form->addRow(QStringLiteral("E-mail"), email);
    form->addRow(QStringLiteral("Position"), position);
    form->addRow(QStringLiteral("Monthly salary"), salary);
    form->addRow(QStringLiteral("Shift"), shift);
    form->addRow(QStringLiteral("Performance"), rating);
    form->addRow(QStringLiteral("Joined"), hired);
    form->addRow(QString(), active);
    column->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primaryButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    models::Employee edited = *existing;
    try {
        edited.setName(name->text().trimmed());
        edited.setPhone(phone->text().trimmed());
        edited.setEmail(email->text().trimmed());
        edited.setPosition(position->currentText());
        edited.setSalary(Money::fromRupees(salary->value()));
        edited.setShift(shift->currentText().trimmed());
        edited.setPerformanceRating(rating->value());
        edited.setHiredDate(hired->date());
        edited.setActive(active->isChecked());
    } catch (const core::ValidationException& e) {
        m_ctx.notifications().notify(QStringLiteral("Invalid details"),
                                     QString::fromUtf8(e.what()), 3);
        return;
    }

    const auto saved = m_ctx.employees().update(edited);
    if (saved.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Staff saved"),
                                     QStringLiteral("%1's record was updated.").arg(edited.name()),
                                     1);
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not save"), saved.error(), 3);
    }
}

void EmployeesPage::onDeactivate() {
    if (!m_ctx.auth().hasRole(models::UserRole::Manager)) {
        m_ctx.notifications().notify(QStringLiteral("Not allowed"),
                                     QStringLiteral("Deactivating staff requires manager "
                                                    "access."),
                                     3);
        return;
    }
    const int id = selectedEmployeeId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Nobody selected"),
                                     QStringLiteral("Choose a staff member first."), 2);
        return;
    }
    const std::optional<models::Employee> existing = m_ctx.employees().byId(id);
    const QString who = existing ? existing->name() : QStringLiteral("this employee");

    QMessageBox confirm(this);
    confirm.setWindowTitle(QStringLiteral("Deactivate staff"));
    confirm.setIcon(QMessageBox::Warning);
    confirm.setText(QStringLiteral("Deactivate %1?").arg(who));
    confirm.setInformativeText(QStringLiteral("The record is kept — payroll history, attendance "
                                              "and audit entries stay intact — but the employee "
                                              "stops appearing in active lists."));
    confirm.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.button(QMessageBox::Yes)->setText(QStringLiteral("Deactivate"));
    confirm.button(QMessageBox::Yes)->setObjectName(QStringLiteral("dangerButton"));
    confirm.button(QMessageBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    if (confirm.exec() != QMessageBox::Yes) return;

    const auto done = m_ctx.employees().deactivate(id);
    if (done.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Staff deactivated"),
                                     QStringLiteral("%1 no longer appears on the active roster.")
                                         .arg(who),
                                     1);
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not deactivate"), done.error(), 3);
    }
}

void EmployeesPage::onMarkAttendance() {
    if (!m_ctx.auth().hasRole(models::UserRole::Manager)) {
        m_ctx.notifications().notify(QStringLiteral("Not allowed"),
                                     QStringLiteral("Marking attendance requires manager "
                                                    "access."),
                                     3);
        return;
    }
    const int id = selectedEmployeeId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Nobody selected"),
                                     QStringLiteral("Choose a staff member first."), 2);
        return;
    }
    const std::optional<models::Employee> existing = m_ctx.employees().byId(id);
    if (!existing) return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Mark attendance"));
    dialog.setMinimumWidth(400);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(22, 22, 22, 22);
    column->setSpacing(16);
    column->addWidget(makeLabel(existing->name(), QStringLiteral("sectionTitle"), &dialog, 16,
                                QFont::DemiBold));
    column->addWidget(makeLabel(QStringLiteral("One row per employee per day — marking the same "
                                               "day twice corrects it rather than duplicating "
                                               "it."),
                                QStringLiteral("mutedLabel"), &dialog, 11));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    auto* monthEdit = findChild<QDateEdit*>(QStringLiteral("attendanceMonth"));
    QDate defaultDay = QDate::currentDate();
    if (monthEdit) {
        const QDate viewed = monthEdit->date();
        if (viewed.year() != defaultDay.year() || viewed.month() != defaultDay.month()) {
            defaultDay = viewed;
        }
    }
    auto* day = new QDateEdit(defaultDay, &dialog);
    day->setCalendarPopup(true);
    day->setDisplayFormat(QStringLiteral("dd MMM yyyy"));
    day->setMinimumHeight(kControlH);
    auto* status = new QComboBox(&dialog);
    status->addItems(attendanceTokens());
    status->setMinimumHeight(kControlH);
    auto* checkIn = new QTimeEdit(QTime(10, 0), &dialog);
    checkIn->setDisplayFormat(QStringLiteral("HH:mm"));
    checkIn->setMinimumHeight(kControlH);
    auto* checkOut = new QTimeEdit(QTime(19, 0), &dialog);
    checkOut->setDisplayFormat(QStringLiteral("HH:mm"));
    checkOut->setMinimumHeight(kControlH);

    form->addRow(QStringLiteral("Day"), day);
    form->addRow(QStringLiteral("Status"), status);
    form->addRow(QStringLiteral("Check in"), checkIn);
    form->addRow(QStringLiteral("Check out"), checkOut);
    column->addLayout(form);

    // ABSENT and LEAVE days carry no clock times — the service's default arguments say so, and
    // the form mirrors that instead of collecting numbers it would throw away.
    const auto syncTimes = [checkIn, checkOut, status]() {
        const bool present = status->currentText() == QLatin1String("PRESENT");
        checkIn->setEnabled(present);
        checkOut->setEnabled(present);
    };
    connect(status, &QComboBox::currentTextChanged, &dialog,
            [syncTimes](const QString&) { syncTimes(); });
    syncTimes();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Record"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primaryButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    const bool present = status->currentText() == QLatin1String("PRESENT");
    const auto marked =
        present ? m_ctx.employees().markAttendance(id, day->date(), status->currentText(),
                                                   checkIn->time(), checkOut->time())
                : m_ctx.employees().markAttendance(id, day->date(), status->currentText());
    if (marked.isOk()) {
        if (monthEdit) monthEdit->setDate(day->date());
        m_ctx.notifications().notify(QStringLiteral("Attendance recorded"),
                                     QStringLiteral("%1 marked %2 on %3.")
                                         .arg(existing->name(),
                                              status->currentText().toLower(),
                                              day->date().toString(
                                                  QStringLiteral("dd MMM yyyy"))),
                                     1);
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not record"), marked.error(), 3);
    }
}

void EmployeesPage::onShowPayroll() {
    try {
        const Palette& p = theme();

        // @oop-concept Runtime Polymorphism :: one loop, four pay formulas. The base pointer
        // never reveals which subclass it holds; monthlyPay() resolves it.
        const std::vector<std::unique_ptr<models::Employee>> roster = m_ctx.employees().staff();

        beginRepopulate(m_payroll);
        m_payroll->setSortingEnabled(false);
        int rowIndex = 0;
        for (const std::unique_ptr<models::Employee>& person : roster) {
            if (!person || !person->isActive()) continue;

            const Money base = person->salary();
            const Money pay = person->monthlyPay();
            const Money extras = pay - base;

            m_payroll->insertRow(rowIndex);
            m_payroll->setItem(rowIndex, 0, textCell(person->name()));

            auto* roleCell = textCell(person->roleName());
            roleCell->setForeground(p.primary);
            m_payroll->setItem(rowIndex, 1, roleCell);

            m_payroll->setItem(rowIndex, 2,
                               numberCell(core::formatNpr(base),
                                          static_cast<double>(base.paisa())));

            auto* extrasCell = numberCell(extras.isZero() ? QStringLiteral("—")
                                                          : core::formatNpr(extras),
                                          static_cast<double>(extras.paisa()));
            extrasCell->setForeground(extras.isZero() ? p.textMuted : p.success);
            extrasCell->setToolTip(QStringLiteral("Whatever this role adds on top of base pay: "
                                                  "tips, overtime or a bonus."));
            m_payroll->setItem(rowIndex, 3, extrasCell);

            auto* payCell = numberCell(core::formatNpr(pay), static_cast<double>(pay.paisa()));
            QFont payFont = payCell->font();
            payFont.setWeight(QFont::DemiBold);
            payCell->setFont(payFont);
            m_payroll->setItem(rowIndex, 4, payCell);
            ++rowIndex;
        }
        m_payroll->setSortingEnabled(true);

        if (auto* stack = findChild<QStackedWidget*>(QStringLiteral("employeesPayrollStack"))) {
            stack->setCurrentIndex(rowIndex == 0 ? 1 : 0);
        }

        // The headline figure comes from the service's own preview, so the screen and the
        // service can never quietly disagree about the payroll bill.
        const auto preview = m_ctx.employees().payrollPreview();
        const Money total = core::sumMoney(
            preview, [](const std::tuple<QString, QString, Money>& row) {
                return std::get<2>(row);
            });
        if (auto* chip = findChild<QLabel*>(QStringLiteral("payrollTotalChip"))) {
            chip->setText(QStringLiteral("%1 staff  ·  %2")
                              .arg(preview.size())
                              .arg(core::formatNpr(total)));
            styleChip(chip, preview.empty() ? p.textMuted : p.primary);
        }
    } catch (const std::exception& e) {
        m_ctx.notifications().notify(QStringLiteral("Payroll"), QString::fromUtf8(e.what()), 3);
    }
}

// ============================================================================================
//  Helpers
// ============================================================================================

int EmployeesPage::selectedEmployeeId() const {
    if (!m_table) return 0;
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_table->rowCount()) return 0;
    if (!m_table->selectionModel() || !m_table->selectionModel()->hasSelection()) return 0;
    const QTableWidgetItem* cell = m_table->item(row, 0);
    return cell ? cell->data(Qt::UserRole).toInt() : 0;
}

}  // namespace aluchop::gui

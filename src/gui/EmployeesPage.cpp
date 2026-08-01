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
#include <memory>
#include <tuple>
#include <vector>

#include <QAbstractAnimation>
#include <QAbstractItemView>
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
#include "aluchop/models/Customer.hpp"
#include "aluchop/models/Employee.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/StaffCustomer.hpp"
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
    staffCol->addWidget(makeLabel(QStringLiteral("Staff"), QStringLiteral("sectionTitle"),
                                  staffPanel, 13, QFont::DemiBold));

    m_table = new ElegantTable(
        QStringList{QStringLiteral("Name"), QStringLiteral("Role"), QStringLiteral("Position"),
                    QStringLiteral("Shift"), QStringLiteral("Salary"), QStringLiteral("Rating"),
                    QStringLiteral("Loyalty"), QStringLiteral("Status")},
        staffPanel);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column <= 7; ++column) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
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
    staffCol->addWidget(staffStack, 1);
    leftColumn->addWidget(staffPanel, 3);

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
    auto* payrollBtn = makeButton(QStringLiteral("Recompute"), QStringLiteral("ghostButton"),
                                  payrollPanel);
    payrollHead->addWidget(payrollBtn);
    payrollCol->addLayout(payrollHead);

    payrollCol->addWidget(
        makeLabel(QStringLiteral("Base salary plus each role's own extras — tips for a waiter, "
                                 "overtime for a chef, a bonus for a manager — all resolved "
                                 "through one virtual monthlyPay() call."),
                  QStringLiteral("mutedLabel"), payrollPanel, 11));

    m_payroll = new ElegantTable(
        QStringList{QStringLiteral("Name"), QStringLiteral("Role"), QStringLiteral("Base"),
                    QStringLiteral("Extras"), QStringLiteral("Monthly pay")},
        payrollPanel);
    m_payroll->verticalHeader()->setDefaultSectionSize(38);
    m_payroll->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column <= 4; ++column) {
        m_payroll->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }

    auto* payrollEmpty = new EmptyState(QStringLiteral("No active staff"),
                                        QStringLiteral("Payroll is computed from active "
                                                       "employees only."),
                                        payrollPanel);
    auto* payrollStack = new QStackedWidget(payrollPanel);
    payrollStack->setObjectName(QStringLiteral("employeesPayrollStack"));
    payrollStack->addWidget(m_payroll);
    payrollStack->addWidget(payrollEmpty);
    payrollCol->addWidget(payrollStack, 1);
    leftColumn->addWidget(payrollPanel, 2);
    body->addLayout(leftColumn, 3);

    // Right column: attendance ---------------------------------------------------------------
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
    m_attendance->verticalHeader()->setDefaultSectionSize(36);
    m_attendance->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column <= 3; ++column) {
        m_attendance->horizontalHeader()->setSectionResizeMode(column,
                                                               QHeaderView::ResizeToContents);
    }

    auto* attendanceEmpty =
        new EmptyState(QStringLiteral("Nothing recorded"),
                       QStringLiteral("Pick a staff member and a month, then mark the days."),
                       attendancePanel);
    auto* attendanceStack = new QStackedWidget(attendancePanel);
    attendanceStack->setObjectName(QStringLiteral("employeesAttendanceStack"));
    attendanceStack->addWidget(m_attendance);
    attendanceStack->addWidget(attendanceEmpty);
    attendanceCol->addWidget(attendanceStack, 1);

    body->addWidget(attendancePanel, 2);
    page->addLayout(body, 1);

    // --- wiring -----------------------------------------------------------------------------
    connect(hireBtn, &QPushButton::clicked, this, &EmployeesPage::onHire);
    connect(emptyHire, &QPushButton::clicked, this, &EmployeesPage::onHire);
    connect(editBtn, &QPushButton::clicked, this, &EmployeesPage::onEdit);
    connect(m_table, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*) { onEdit(); });
    connect(deactivateBtn, &QPushButton::clicked, this, &EmployeesPage::onDeactivate);
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
                role == QLatin1String("staffDeactivate") || role == QLatin1String("staffMark")) {
                button->setEnabled(mayManage);
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
        QHash<QString, QString> loyaltyByPhone;  // phone key -> "340 pts · staff −10%"
        if (!staffPhones.isEmpty()) {
            for (const models::Customer& guest : m_ctx.customers().all()) {
                const QString key = phoneKey(guest.phone());
                if (key.isEmpty() || !staffPhones.contains(key)) continue;
                const std::optional<models::StaffCustomer> fused =
                    m_ctx.employees().staffCustomerFor(guest.id());
                if (!fused) continue;
                // One object, one identity: name() comes from the shared virtual Person base,
                // loyaltyPoints() from the Customer branch, staffDiscountPercent() is its own.
                loyaltyByPhone.insert(key, QStringLiteral("%1 pts · staff −%2%")
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
            m_table->setItem(rowIndex, 0, nameCell);

            // roleName() is virtual: Waiter/Chef/Manager/Admin each answer for themselves.
            auto* roleCell = textCell(person->roleName());
            roleCell->setForeground(p.primary);
            roleCell->setToolTip(person->displayLabel());
            m_table->setItem(rowIndex, 1, roleCell);

            auto* positionCell = textCell(person->position());
            positionCell->setForeground(p.textMuted);
            positionCell->setToolTip(QStringLiteral("The stored position token is what chooses "
                                                    "the concrete C++ subclass."));
            m_table->setItem(rowIndex, 2, positionCell);

            m_table->setItem(rowIndex, 3, textCell(person->shift()));

            const Money salary = person->salary();
            m_table->setItem(rowIndex, 4,
                             numberCell(core::formatNpr(salary),
                                        static_cast<double>(salary.paisa())));

            auto* ratingCell = new SortableCell(ratingBar(person->performanceRating()),
                                                person->performanceRating());
            ratingCell->setTextAlignment(Qt::AlignCenter);
            ratingCell->setForeground(person->performanceRating() >= 4 ? p.success
                                                                       : p.textMuted);
            ratingCell->setToolTip(QStringLiteral("Performance rating %1 of 5")
                                       .arg(person->performanceRating()));
            m_table->setItem(rowIndex, 5, ratingCell);

            const QString loyalty = loyaltyByPhone.value(phoneKey(person->phone()));
            auto* loyaltyCell = textCell(loyalty.isEmpty() ? QStringLiteral("—") : loyalty);
            if (!loyalty.isEmpty()) {
                ++fusedCount;
                loyaltyCell->setForeground(p.secondary);
                QFont loyaltyFont = loyaltyCell->font();
                loyaltyFont.setWeight(QFont::DemiBold);
                loyaltyCell->setFont(loyaltyFont);
                loyaltyCell->setToolTip(
                    QStringLiteral("%1 is on the payroll AND in the loyalty programme — one "
                                   "StaffCustomer object with a single shared identity.")
                        .arg(person->name()));
            } else {
                loyaltyCell->setForeground(p.textMuted);
            }
            m_table->setItem(rowIndex, 6, loyaltyCell);

            auto* statusCell = textCell(person->isActive() ? QStringLiteral("Active")
                                                           : QStringLiteral("Inactive"));
            statusCell->setForeground(person->isActive() ? p.success : p.danger);
            m_table->setItem(rowIndex, 7, statusCell);

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

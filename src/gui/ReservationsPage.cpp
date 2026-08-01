/**
 * @file ReservationsPage.cpp
 * @brief Screen 7 — table bookings: day picker, availability, seating and cancellation.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * services::ReservationService answers the only hard question — which tables are free for a given
 * window and party size — by checking overlaps down in the persistence layer. The table picker on
 * this screen is populated purely from that answer, so a double booking cannot be created through
 * the UI at all; when nothing is free the screen says *why*, naming the bookings that are holding
 * the tables. The service re-validates on book() regardless, because a UI is never a security
 * boundary.
 *
 * Status actions follow the reservation lifecycle: Booked → Seated → Completed, with Cancelled
 * and No-show as terminal states — and they are models::ReservationStatus values throughout, not
 * free-form strings.
 */

#include "aluchop/gui/ReservationsPage.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include <QAbstractAnimation>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
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
#include <QSize>
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

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/gui/Widgets.hpp"
#include "aluchop/models/Customer.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/Reservation.hpp"
#include "aluchop/models/Table.hpp"
#include "aluchop/services/AppContext.hpp"

namespace aluchop::gui {
namespace {

// --- house measurements, identical across the four management screens ------------------------
constexpr int kPageMargin = 24;
constexpr int kPageSpacing = 18;
constexpr int kPanelPad = 18;
constexpr int kControlH = 38;
constexpr int kFadeMs = 220;
constexpr int kDefaultSittingMin = 90;  ///< the house sitting, matching Reservation's own default

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
/// @oop-concept Method Overriding :: QTableWidgetItem::operator< is overridden so the Time column
///              sorts chronologically rather than alphabetically
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

/// @return the human-facing name of a booking state.
/// @oop-concept Enumerations (surfaced) :: the status column is a ReservationStatus, and this is
///              the single place its enumerators become English
QString statusLabel(models::ReservationStatus status) {
    switch (status) {
        case models::ReservationStatus::Booked:
            return QStringLiteral("Booked");
        case models::ReservationStatus::Seated:
            return QStringLiteral("Seated");
        case models::ReservationStatus::Completed:
            return QStringLiteral("Completed");
        case models::ReservationStatus::Cancelled:
            return QStringLiteral("Cancelled");
        case models::ReservationStatus::NoShow:
            break;
    }
    return QStringLiteral("No-show");
}

/// @return the palette colour that carries a booking state's meaning.
QColor statusColour(models::ReservationStatus status) {
    const Palette& p = theme();
    switch (status) {
        case models::ReservationStatus::Booked:
            return p.primary;
        case models::ReservationStatus::Seated:
            return p.success;
        case models::ReservationStatus::Completed:
            return p.textMuted;
        case models::ReservationStatus::Cancelled:
        case models::ReservationStatus::NoShow:
            break;
    }
    return p.danger;
}

/// @return true while a booking still holds its table (and therefore still blocks the slot).
bool blocksTable(models::ReservationStatus status) {
    return status == models::ReservationStatus::Booked ||
           status == models::ReservationStatus::Seated;
}

/// @brief Composes the local start instant for a booking.
///
/// The picker gives a calendar day and the form gives a wall-clock time; joining them in local
/// time is what the user means, and QDateTime comparisons downstream are absolute-instant
/// comparisons, so the overlap check stays correct however the repository chooses to store it.
QDateTime startInstant(QDate day, QTime time) { return QDateTime(day, time); }

}  // namespace

// ============================================================================================
//  Construction
// ============================================================================================

ReservationsPage::ReservationsPage(services::AppContext& ctx, QWidget* parent)
    : Page(ctx, parent) {
    setObjectName(QStringLiteral("reservationsPage"));

    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    page->setSpacing(kPageSpacing);

    // --- header band ------------------------------------------------------------------------
    auto* header = new QHBoxLayout;
    header->setSpacing(12);

    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    titles->addWidget(makeLabel(pageTitle(), QStringLiteral("pageTitle"), this, 24, QFont::Bold));
    auto* subtitle = makeLabel(QStringLiteral("Opening the booking sheet…"),
                               QStringLiteral("mutedLabel"), this, 11);
    subtitle->setProperty("aluchopRole", QStringLiteral("reservationsSubtitle"));
    titles->addWidget(subtitle);
    header->addLayout(titles);
    header->addStretch(1);

    auto* seatBtn = makeButton(QStringLiteral("Seat"), QStringLiteral("ghostButton"), this);
    auto* completeBtn = makeButton(QStringLiteral("Complete"), QStringLiteral("ghostButton"),
                                   this);
    auto* editBtn = makeButton(QStringLiteral("Edit"), QStringLiteral("ghostButton"), this);
    auto* cancelBtn = makeButton(QStringLiteral("Cancel booking"), QStringLiteral("dangerButton"),
                                 this);
    seatBtn->setProperty("aluchopRole", QStringLiteral("resSeat"));
    completeBtn->setProperty("aluchopRole", QStringLiteral("resComplete"));
    editBtn->setProperty("aluchopRole", QStringLiteral("resEdit"));
    cancelBtn->setProperty("aluchopRole", QStringLiteral("resCancel"));
    header->addWidget(seatBtn);
    header->addWidget(completeBtn);
    header->addWidget(editBtn);
    header->addWidget(cancelBtn);
    page->addLayout(header);

    // --- day band ---------------------------------------------------------------------------
    auto* dayRow = new QHBoxLayout;
    dayRow->setSpacing(10);

    auto* prevDay = makeButton(QStringLiteral("‹"), QStringLiteral("ghostButton"), this);
    prevDay->setFixedWidth(44);
    prevDay->setToolTip(QStringLiteral("Previous day"));
    m_day = new QDateEdit(QDate::currentDate(), this);
    m_day->setObjectName(QStringLiteral("reservationDay"));
    m_day->setCalendarPopup(true);
    m_day->setDisplayFormat(QStringLiteral("dddd, dd MMMM yyyy"));
    m_day->setMinimumHeight(kControlH);
    auto* nextDay = makeButton(QStringLiteral("›"), QStringLiteral("ghostButton"), this);
    nextDay->setFixedWidth(44);
    nextDay->setToolTip(QStringLiteral("Next day"));
    auto* todayBtn = makeButton(QStringLiteral("Today"), QStringLiteral("ghostButton"), this);

    dayRow->addWidget(prevDay);
    dayRow->addWidget(m_day);
    dayRow->addWidget(nextDay);
    dayRow->addWidget(todayBtn);
    dayRow->addSpacing(12);

    auto* coversChip = makeChip(QStringLiteral("—"), this);
    coversChip->setObjectName(QStringLiteral("reservationCoversChip"));
    auto* liveChip = makeChip(QStringLiteral("—"), this);
    liveChip->setObjectName(QStringLiteral("reservationLiveChip"));
    dayRow->addWidget(coversChip);
    dayRow->addWidget(liveChip);
    dayRow->addStretch(1);
    page->addLayout(dayRow);

    // --- body -------------------------------------------------------------------------------
    auto* body = new QHBoxLayout;
    body->setSpacing(kPageSpacing);

    // Booking sheet ---------------------------------------------------------------------------
    auto* sheetPanel = new GlassPanel(this);
    auto* sheetCol = new QVBoxLayout(sheetPanel);
    sheetCol->setContentsMargins(kPanelPad, kPanelPad, kPanelPad, kPanelPad);
    sheetCol->setSpacing(12);
    sheetCol->addWidget(makeLabel(QStringLiteral("Booking sheet"),
                                  QStringLiteral("sectionTitle"), sheetPanel, 13,
                                  QFont::DemiBold));

    m_list = new ElegantTable(
        QStringList{QStringLiteral("Time"), QStringLiteral("Guest"), QStringLiteral("Guests"),
                    QStringLiteral("Table"), QStringLiteral("Status"),
                    QStringLiteral("Special request")},
        sheetPanel);
    m_list->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_list->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_list->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_list->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_list->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_list->setMinimumWidth(520);

    auto* sheetEmpty = new EmptyState(QStringLiteral("No bookings for this day"),
                                      QStringLiteral("Take one on the right — the table picker "
                                                     "only offers tables that are genuinely "
                                                     "free."),
                                      sheetPanel);
    auto* sheetStack = new QStackedWidget(sheetPanel);
    sheetStack->setObjectName(QStringLiteral("reservationSheetStack"));
    sheetStack->addWidget(m_list);
    sheetStack->addWidget(sheetEmpty);
    sheetCol->addWidget(sheetStack, 1);
    body->addWidget(sheetPanel, 3);

    // New-booking panel -------------------------------------------------------------------------
    auto* formPanel = new GlassPanel(this);
    formPanel->setMinimumWidth(360);
    auto* formCol = new QVBoxLayout(formPanel);
    formCol->setContentsMargins(kPanelPad, kPanelPad, kPanelPad, kPanelPad);
    formCol->setSpacing(12);
    formCol->addWidget(makeLabel(QStringLiteral("Take a booking"), QStringLiteral("sectionTitle"),
                                 formPanel, 13, QFont::DemiBold));
    formCol->addWidget(makeLabel(QStringLiteral("Availability updates as you type — the picker "
                                                "never shows a table that is already held."),
                                 QStringLiteral("mutedLabel"), formPanel, 11));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto* guestName = new QLineEdit(formPanel);
    guestName->setObjectName(QStringLiteral("resGuestName"));
    guestName->setPlaceholderText(QStringLiteral("Name on the booking"));
    guestName->setMinimumHeight(kControlH);

    auto* guestPhone = new QLineEdit(formPanel);
    guestPhone->setObjectName(QStringLiteral("resGuestPhone"));
    guestPhone->setPlaceholderText(QStringLiteral("98XXXXXXXX"));
    guestPhone->setMinimumHeight(kControlH);

    auto* startTime = new QTimeEdit(QTime(19, 0), formPanel);
    startTime->setObjectName(QStringLiteral("resStartTime"));
    startTime->setDisplayFormat(QStringLiteral("HH:mm"));
    startTime->setMinimumHeight(kControlH);

    auto* duration = new QSpinBox(formPanel);
    duration->setObjectName(QStringLiteral("resDuration"));
    duration->setRange(15, 360);
    duration->setSingleStep(15);
    duration->setValue(kDefaultSittingMin);
    duration->setSuffix(QStringLiteral(" min"));

    auto* guests = new QSpinBox(formPanel);
    guests->setObjectName(QStringLiteral("resGuests"));
    guests->setRange(1, 40);
    guests->setValue(2);
    guests->setSuffix(QStringLiteral(" guests"));

    m_tablePicker = new QComboBox(formPanel);
    m_tablePicker->setObjectName(QStringLiteral("resTablePicker"));
    m_tablePicker->setMinimumHeight(kControlH);

    auto* request = new QLineEdit(formPanel);
    request->setObjectName(QStringLiteral("resRequest"));
    request->setPlaceholderText(QStringLiteral("window seat, birthday cake…"));
    request->setMinimumHeight(kControlH);

    form->addRow(QStringLiteral("Guest"), guestName);
    form->addRow(QStringLiteral("Phone"), guestPhone);
    form->addRow(QStringLiteral("From"), startTime);
    form->addRow(QStringLiteral("For"), duration);
    form->addRow(QStringLiteral("Party"), guests);
    form->addRow(QStringLiteral("Table"), m_tablePicker);
    form->addRow(QStringLiteral("Request"), request);
    formCol->addLayout(form);

    auto* recognised = makeLabel(QString(), QString(), formPanel, 11);
    recognised->setObjectName(QStringLiteral("resRecognised"));
    recognised->setWordWrap(true);
    recognised->setVisible(false);
    formCol->addWidget(recognised);

    auto* availability = makeLabel(QString(), QString(), formPanel, 11);
    availability->setObjectName(QStringLiteral("resAvailability"));
    availability->setWordWrap(true);
    formCol->addWidget(availability);

    formCol->addStretch(1);

    auto* bookBtn = makeButton(QStringLiteral("Book the table"), QStringLiteral("primaryButton"),
                               formPanel);
    bookBtn->setProperty("aluchopRole", QStringLiteral("resBook"));
    formCol->addWidget(bookBtn);

    body->addWidget(formPanel, 2);
    page->addLayout(body, 1);

    // --- wiring -----------------------------------------------------------------------------
    connect(m_day, &QDateEdit::dateChanged, this, [this](const QDate&) {
        if (BusyGuard::busy(this)) return;
        onDayChanged();
    });
    connect(prevDay, &QPushButton::clicked, this,
            [this]() { m_day->setDate(m_day->date().addDays(-1)); });
    connect(nextDay, &QPushButton::clicked, this,
            [this]() { m_day->setDate(m_day->date().addDays(1)); });
    connect(todayBtn, &QPushButton::clicked, this,
            [this]() { m_day->setDate(QDate::currentDate()); });

    // Live availability: any change to the window or the party size re-asks the service.
    const auto reask = [this]() {
        if (BusyGuard::busy(this)) return;
        refresh();
    };
    connect(startTime, &QTimeEdit::timeChanged, this, [reask](const QTime&) { reask(); });
    connect(duration, &QSpinBox::valueChanged, this, [reask](int) { reask(); });
    connect(guests, &QSpinBox::valueChanged, this, [reask](int) { reask(); });
    connect(guestPhone, &QLineEdit::textChanged, this, [reask](const QString&) { reask(); });

    connect(bookBtn, &QPushButton::clicked, this, &ReservationsPage::onBook);
    connect(editBtn, &QPushButton::clicked, this, &ReservationsPage::onEdit);
    connect(m_list, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*) { onEdit(); });
    connect(cancelBtn, &QPushButton::clicked, this, &ReservationsPage::onCancel);
    connect(seatBtn, &QPushButton::clicked, this, &ReservationsPage::onSeat);
    connect(completeBtn, &QPushButton::clicked, this, &ReservationsPage::onComplete);
    connect(m_list, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (BusyGuard::busy(this)) return;
        refresh();
    });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            &ReservationsPage::refresh);

    new EntranceAnimator(this);
}

// ============================================================================================
//  Page interface
// ============================================================================================

QString ReservationsPage::pageTitle() const { return QStringLiteral("Reservations"); }

void ReservationsPage::refresh() {
    // Must be idempotent and must never throw — Page's contract.
    if (BusyGuard::busy(this)) return;
    BusyGuard guard(this);
    try {
        const Palette& p = theme();

        const QDate day = m_day->date();
        const int previousId = selectedReservationId();

        // Tables are needed by name in three places, so they are fetched once.
        const std::vector<models::Table> allTables = m_ctx.reservations().tables();
        QHash<int, QString> tableNames;
        QHash<int, int> tableCapacity;
        for (const models::Table& table : allTables) {
            tableNames.insert(table.id(), table.name());
            tableCapacity.insert(table.id(), table.capacity());
        }

        // --- the day's sheet --------------------------------------------------------------------
        const std::vector<models::Reservation> bookings = m_ctx.reservations().onDay(day);

        beginRepopulate(m_list);
        int rowIndex = 0;
        int covers = 0;
        int liveBookings = 0;
        for (const models::Reservation& booking : bookings) {
            m_list->insertRow(rowIndex);

            const QDateTime start = booking.startsAt().toLocalTime();
            const QDateTime end = booking.endsAt().toLocalTime();
            auto* timeCell = new SortableCell(
                QStringLiteral("%1 – %2").arg(start.time().toString(QStringLiteral("HH:mm")),
                                              end.time().toString(QStringLiteral("HH:mm"))),
                static_cast<double>(start.time().msecsSinceStartOfDay()));
            timeCell->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            timeCell->setData(Qt::UserRole, booking.id());
            timeCell->setData(Qt::UserRole + 1, static_cast<int>(booking.status()));
            QFont timeFont = timeCell->font();
            timeFont.setWeight(QFont::DemiBold);
            timeCell->setFont(timeFont);
            m_list->setItem(rowIndex, 0, timeCell);

            QString guestText = booking.customerName();
            if (!booking.phone().isEmpty()) {
                guestText += QStringLiteral("  ·  %1").arg(booking.phone());
            }
            auto* guestCell = textCell(guestText);
            if (booking.customerId() != 0) {
                guestCell->setToolTip(QStringLiteral("Linked to a loyalty customer."));
            }
            m_list->setItem(rowIndex, 1, guestCell);

            m_list->setItem(rowIndex, 2,
                            numberCell(QString::number(booking.guests()), booking.guests()));

            const QString tableName = tableNames.value(booking.tableId());
            auto* tableCell = textCell(tableName.isEmpty()
                                           ? QStringLiteral("#%1").arg(booking.tableId())
                                           : tableName,
                                       Qt::AlignCenter);
            const int capacity = tableCapacity.value(booking.tableId(), 0);
            if (capacity > 0) {
                tableCell->setToolTip(QStringLiteral("Seats %1").arg(capacity));
            }
            m_list->setItem(rowIndex, 3, tableCell);

            auto* statusCell = textCell(statusLabel(booking.status()));
            statusCell->setForeground(statusColour(booking.status()));
            QFont statusFont = statusCell->font();
            statusFont.setWeight(QFont::DemiBold);
            statusCell->setFont(statusFont);
            m_list->setItem(rowIndex, 4, statusCell);

            auto* requestCell = textCell(booking.specialRequest().isEmpty()
                                             ? QStringLiteral("—")
                                             : booking.specialRequest());
            if (booking.specialRequest().isEmpty()) requestCell->setForeground(p.textMuted);
            m_list->setItem(rowIndex, 5, requestCell);

            if (blocksTable(booking.status())) {
                covers += booking.guests();
                ++liveBookings;
            } else {
                for (int column = 0; column < m_list->columnCount(); ++column) {
                    if (QTableWidgetItem* cell = m_list->item(rowIndex, column)) {
                        if (column != 4) cell->setForeground(p.textMuted);
                    }
                }
            }
            ++rowIndex;
        }
        endRepopulate(m_list);

        if (auto* stack = findChild<QStackedWidget*>(QStringLiteral("reservationSheetStack"))) {
            stack->setCurrentIndex(rowIndex == 0 ? 1 : 0);
            if (auto* empty = dynamic_cast<EmptyState*>(stack->widget(1))) {
                empty->setText(QStringLiteral("No bookings on %1")
                                   .arg(day.toString(QStringLiteral("dddd dd MMM"))),
                               QStringLiteral("Take one on the right — the table picker only "
                                              "offers tables that are genuinely free."));
            }
        }

        if (previousId != 0) {
            for (int row = 0; row < m_list->rowCount(); ++row) {
                const QTableWidgetItem* cell = m_list->item(row, 0);
                if (cell && cell->data(Qt::UserRole).toInt() == previousId) {
                    m_list->selectRow(row);
                    break;
                }
            }
        }

        // --- status actions follow the lifecycle -------------------------------------------------
        const int selectedId = selectedReservationId();
        models::ReservationStatus selectedStatus = models::ReservationStatus::Completed;
        bool hasSelection = false;
        for (const models::Reservation& booking : bookings) {
            if (booking.id() == selectedId) {
                selectedStatus = booking.status();
                hasSelection = true;
                break;
            }
        }
        for (QPushButton* button : findChildren<QPushButton*>()) {
            const QString role = button->property("aluchopRole").toString();
            if (role == QLatin1String("resSeat")) {
                button->setEnabled(hasSelection &&
                                   selectedStatus == models::ReservationStatus::Booked);
            } else if (role == QLatin1String("resComplete")) {
                button->setEnabled(hasSelection &&
                                   selectedStatus == models::ReservationStatus::Seated);
            } else if (role == QLatin1String("resCancel")) {
                button->setEnabled(hasSelection && blocksTable(selectedStatus));
            } else if (role == QLatin1String("resEdit")) {
                button->setEnabled(hasSelection && blocksTable(selectedStatus));
            }
        }

        // --- live availability for the form ------------------------------------------------------
        auto* startTime = findChild<QTimeEdit*>(QStringLiteral("resStartTime"));
        auto* duration = findChild<QSpinBox*>(QStringLiteral("resDuration"));
        auto* guestsBox = findChild<QSpinBox*>(QStringLiteral("resGuests"));
        auto* availability = findChild<QLabel*>(QStringLiteral("resAvailability"));
        auto* recognised = findChild<QLabel*>(QStringLiteral("resRecognised"));
        auto* phoneField = findChild<QLineEdit*>(QStringLiteral("resGuestPhone"));
        auto* nameField = findChild<QLineEdit*>(QStringLiteral("resGuestName"));

        int freeNow = 0;
        if (startTime && duration && guestsBox && availability && m_tablePicker) {
            const QDateTime start = startInstant(day, startTime->time());
            const int minutes = duration->value();
            const int partySize = guestsBox->value();

            // The service is the single authority on availability.
            const std::vector<models::Table> free =
                m_ctx.reservations().availableTables(start, minutes, partySize);
            freeNow = static_cast<int>(free.size());

            const int previousTable = m_tablePicker->currentData().toInt();
            m_tablePicker->clear();
            for (const models::Table& table : free) {
                m_tablePicker->addItem(QStringLiteral("%1  ·  seats %2")
                                           .arg(table.name())
                                           .arg(table.capacity()),
                                       table.id());
            }
            const int restored = m_tablePicker->findData(previousTable);
            if (restored >= 0) m_tablePicker->setCurrentIndex(restored);
            m_tablePicker->setEnabled(!free.empty());

            // Blocking a double booking in the UI is only half the job — saying *why* is the
            // other half, so the tables that would have fitted are named with their clash.
            if (free.empty()) {
                // Every table that *could* have seated the party is named together with the
                // booking that is holding it, so "no tables" is never a dead end for the user.
                QStringList clashes;
                for (const models::Table& table : allTables) {
                    if (!table.isActive() || table.capacity() < partySize) continue;
                    for (const models::Reservation& booking : bookings) {
                        if (booking.tableId() != table.id()) continue;
                        if (!blocksTable(booking.status())) continue;
                        if (!booking.overlaps(start, minutes)) continue;
                        clashes << QStringLiteral("%1 is held %2–%3 by %4")
                                       .arg(table.name(),
                                            booking.startsAt().toLocalTime().time().toString(
                                                QStringLiteral("HH:mm")),
                                            booking.endsAt().toLocalTime().time().toString(
                                                QStringLiteral("HH:mm")),
                                            booking.customerName());
                        break;
                    }
                }

                QString explanation;
                if (clashes.isEmpty()) {
                    explanation =
                        QStringLiteral("No active table seats %1. Split the party across two "
                                       "bookings, or add a larger table in the floor plan.")
                            .arg(partySize);
                } else {
                    explanation = QStringLiteral("Nothing free for %1 guests at %2 — %3. Try a "
                                                 "later slot or a shorter sitting.")
                                      .arg(partySize)
                                      .arg(startTime->time().toString(QStringLiteral("HH:mm")),
                                           clashes.join(QStringLiteral("; ")));
                }
                availability->setText(explanation);
                availability->setStyleSheet(QStringLiteral("color: %1;").arg(p.danger.name()));
            } else {
                availability->setText(
                    QStringLiteral("%1 table%2 free for %3 guests from %4 for %5 minutes.")
                        .arg(free.size())
                        .arg(free.size() == 1 ? QString() : QStringLiteral("s"))
                        .arg(partySize)
                        .arg(startTime->time().toString(QStringLiteral("HH:mm")))
                        .arg(minutes));
                availability->setStyleSheet(QStringLiteral("color: %1;").arg(p.textMuted.name()));
            }
        }

        for (QPushButton* button : findChildren<QPushButton*>()) {
            if (button->property("aluchopRole").toString() == QLatin1String("resBook")) {
                button->setEnabled(m_tablePicker && m_tablePicker->count() > 0);
            }
        }

        // --- recognise a returning guest by phone --------------------------------------------------
        if (recognised && phoneField) {
            const QString typed = phoneField->text().trimmed();
            std::optional<models::Customer> known;
            if (typed.size() >= 7) known = m_ctx.customers().byPhone(typed);
            if (known) {
                recognised->setVisible(true);
                recognised->setText(QStringLiteral("Recognised: %1 · %2 visits · %3 loyalty "
                                                   "points")
                                        .arg(known->name())
                                        .arg(known->visits())
                                        .arg(known->loyaltyPoints()));
                recognised->setStyleSheet(QStringLiteral("color: %1;").arg(p.success.name()));
                if (nameField && nameField->text().trimmed().isEmpty()) {
                    nameField->setText(known->name());
                }
            } else {
                recognised->setVisible(false);
            }
        }

        // --- chips and subtitle ----------------------------------------------------------------
        if (auto* chip = findChild<QLabel*>(QStringLiteral("reservationCoversChip"))) {
            chip->setText(QStringLiteral("%1 covers").arg(covers));
            styleChip(chip, covers > 0 ? p.primary : p.textMuted);
        }
        if (auto* chip = findChild<QLabel*>(QStringLiteral("reservationLiveChip"))) {
            chip->setText(QStringLiteral("%1 free now").arg(freeNow));
            styleChip(chip, freeNow > 0 ? p.success : p.danger);
        }
        for (QLabel* label : findChildren<QLabel*>()) {
            if (label->property("aluchopRole").toString() ==
                QLatin1String("reservationsSubtitle")) {
                label->setText(QStringLiteral("%1 booking%2 on the sheet  ·  %3 still holding a "
                                              "table  ·  %4 tables in the floor plan")
                                   .arg(bookings.size())
                                   .arg(bookings.size() == 1 ? QString() : QStringLiteral("s"))
                                   .arg(liveBookings)
                                   .arg(allTables.size()));
            }
        }
    } catch (const std::exception& e) {
        m_ctx.notifications().notify(QStringLiteral("Reservations"), QString::fromUtf8(e.what()),
                                     3);
    }
}

// ============================================================================================
//  Slots
// ============================================================================================

void ReservationsPage::onDayChanged() { refresh(); }

void ReservationsPage::onBook() {
    auto* nameField = findChild<QLineEdit*>(QStringLiteral("resGuestName"));
    auto* phoneField = findChild<QLineEdit*>(QStringLiteral("resGuestPhone"));
    auto* startTime = findChild<QTimeEdit*>(QStringLiteral("resStartTime"));
    auto* duration = findChild<QSpinBox*>(QStringLiteral("resDuration"));
    auto* guestsBox = findChild<QSpinBox*>(QStringLiteral("resGuests"));
    auto* request = findChild<QLineEdit*>(QStringLiteral("resRequest"));
    if (!nameField || !phoneField || !startTime || !duration || !guestsBox || !request ||
        !m_tablePicker) {
        return;
    }

    if (m_tablePicker->count() == 0) {
        m_ctx.notifications().notify(QStringLiteral("No table free"),
                                     QStringLiteral("Change the time, the sitting length or the "
                                                    "party size."),
                                     2);
        return;
    }
    const QString guestName = nameField->text().trimmed();
    if (guestName.isEmpty()) {
        m_ctx.notifications().notify(QStringLiteral("Name required"),
                                     QStringLiteral("A booking needs a name to shout across the "
                                                    "dining room."),
                                     2);
        nameField->setFocus();
        return;
    }

    models::Reservation booking;
    try {
        booking = models::Reservation(0, m_tablePicker->currentData().toInt(), guestName,
                                      phoneField->text().trimmed(),
                                      startInstant(m_day->date(), startTime->time()),
                                      duration->value(), guestsBox->value());
        booking.setSpecialRequest(request->text().trimmed());
        booking.setStatus(models::ReservationStatus::Booked);
        // Link the booking to the loyalty record when the phone number is one we know.
        const QString typedPhone = phoneField->text().trimmed();
        if (typedPhone.size() >= 7) {
            if (const auto known = m_ctx.customers().byPhone(typedPhone)) {
                booking.setCustomerId(known->id());
            }
        }
    } catch (const core::ValidationException& e) {
        m_ctx.notifications().notify(QStringLiteral("Invalid booking"),
                                     QString::fromUtf8(e.what()), 3);
        return;
    }

    // The service re-checks availability: the picker is a convenience, never the guarantee.
    const auto booked = m_ctx.reservations().book(booking);
    if (booked.isOk()) {
        m_ctx.notifications().notify(
            QStringLiteral("Table booked"),
            QStringLiteral("%1 · %2 guests at %3")
                .arg(guestName)
                .arg(guestsBox->value())
                .arg(startTime->time().toString(QStringLiteral("HH:mm"))),
            1);
        nameField->clear();
        phoneField->clear();
        request->clear();
        refresh();
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not book"), booked.error(), 3);
        refresh();
    }
}

void ReservationsPage::onEdit() {
    const int id = selectedReservationId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Nothing selected"),
                                     QStringLiteral("Choose a booking on the sheet first."), 2);
        return;
    }

    const std::vector<models::Reservation> bookings = m_ctx.reservations().onDay(m_day->date());
    const auto found = std::find_if(bookings.begin(), bookings.end(),
                                    [id](const models::Reservation& r) { return r.id() == id; });
    if (found == bookings.end()) {
        refresh();
        return;
    }
    const models::Reservation existing = *found;
    const std::vector<models::Table> allTables = m_ctx.reservations().tables();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit booking"));
    dialog.setMinimumWidth(460);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(22, 22, 22, 22);
    column->setSpacing(16);
    column->addWidget(makeLabel(QStringLiteral("Edit %1's booking").arg(existing.customerName()),
                                QStringLiteral("sectionTitle"), &dialog, 16, QFont::DemiBold));
    auto* note = makeLabel(QString(), QString(), &dialog, 11);
    note->setWordWrap(true);
    column->addWidget(note);

    auto* form = new QFormLayout;
    form->setSpacing(10);
    auto* name = new QLineEdit(existing.customerName(), &dialog);
    name->setMinimumHeight(kControlH);
    auto* phone = new QLineEdit(existing.phone(), &dialog);
    phone->setMinimumHeight(kControlH);
    auto* startTime = new QTimeEdit(existing.startsAt().toLocalTime().time(), &dialog);
    startTime->setDisplayFormat(QStringLiteral("HH:mm"));
    startTime->setMinimumHeight(kControlH);
    auto* duration = new QSpinBox(&dialog);
    duration->setRange(15, 360);
    duration->setSingleStep(15);
    duration->setValue(existing.durationMin());
    duration->setSuffix(QStringLiteral(" min"));
    auto* guests = new QSpinBox(&dialog);
    guests->setRange(1, 40);
    guests->setValue(existing.guests());
    guests->setSuffix(QStringLiteral(" guests"));
    auto* tablePicker = new QComboBox(&dialog);
    tablePicker->setMinimumHeight(kControlH);
    auto* request = new QLineEdit(existing.specialRequest(), &dialog);
    request->setMinimumHeight(kControlH);

    form->addRow(QStringLiteral("Guest"), name);
    form->addRow(QStringLiteral("Phone"), phone);
    form->addRow(QStringLiteral("From"), startTime);
    form->addRow(QStringLiteral("For"), duration);
    form->addRow(QStringLiteral("Party"), guests);
    form->addRow(QStringLiteral("Table"), tablePicker);
    form->addRow(QStringLiteral("Request"), request);
    column->addLayout(form);

    // Re-ask the service every time the window changes. The booking's own table has to be added
    // back by hand: availableTables() correctly reports it as taken — it is taken by this very
    // booking — and refusing to offer it would make "change the time by 15 minutes" impossible.
    const QDate day = m_day->date();
    const int currentTableId = existing.tableId();
    const auto reloadTables = [this, tablePicker, startTime, duration, guests, note, day,
                               currentTableId, allTables]() {
        const int previous = tablePicker->currentData().toInt();
        const QDateTime start = startInstant(day, startTime->time());
        const std::vector<models::Table> free =
            m_ctx.reservations().availableTables(start, duration->value(), guests->value());

        tablePicker->clear();
        QSet<int> added;
        for (const models::Table& table : free) {
            tablePicker->addItem(
                QStringLiteral("%1  ·  seats %2").arg(table.name()).arg(table.capacity()),
                table.id());
            added.insert(table.id());
        }
        if (!added.contains(currentTableId)) {
            for (const models::Table& table : allTables) {
                if (table.id() != currentTableId) continue;
                if (table.capacity() < guests->value()) break;
                tablePicker->addItem(QStringLiteral("%1  ·  seats %2  (current)")
                                         .arg(table.name())
                                         .arg(table.capacity()),
                                     table.id());
                added.insert(table.id());
                break;
            }
        }
        const int restore = tablePicker->findData(previous == 0 ? currentTableId : previous);
        if (restore >= 0) tablePicker->setCurrentIndex(restore);

        const Palette& p = theme();
        if (tablePicker->count() == 0) {
            note->setText(QStringLiteral("Nothing seats %1 in that window — every table large "
                                         "enough is already held.")
                              .arg(guests->value()));
            note->setStyleSheet(QStringLiteral("color: %1;").arg(p.danger.name()));
        } else {
            note->setText(QStringLiteral("%1 table option%2 for this window.")
                              .arg(tablePicker->count())
                              .arg(tablePicker->count() == 1 ? QString() : QStringLiteral("s")));
            note->setStyleSheet(QStringLiteral("color: %1;").arg(p.textMuted.name()));
        }
    };
    connect(startTime, &QTimeEdit::timeChanged, &dialog,
            [reloadTables](const QTime&) { reloadTables(); });
    connect(duration, &QSpinBox::valueChanged, &dialog, [reloadTables](int) { reloadTables(); });
    connect(guests, &QSpinBox::valueChanged, &dialog, [reloadTables](int) { reloadTables(); });
    reloadTables();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primaryButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    if (tablePicker->count() == 0) {
        m_ctx.notifications().notify(QStringLiteral("No table free"),
                                     QStringLiteral("The new window has no table that fits."), 2);
        return;
    }

    models::Reservation edited = existing;
    try {
        edited.setCustomerName(name->text().trimmed());
        edited.setPhone(phone->text().trimmed());
        edited.setStartsAt(startInstant(day, startTime->time()));
        edited.setDurationMin(duration->value());
        edited.setGuests(guests->value());
        edited.setTableId(tablePicker->currentData().toInt());
        edited.setSpecialRequest(request->text().trimmed());
    } catch (const core::ValidationException& e) {
        m_ctx.notifications().notify(QStringLiteral("Invalid booking"),
                                     QString::fromUtf8(e.what()), 3);
        return;
    }

    const auto saved = m_ctx.reservations().update(edited);
    if (saved.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Booking updated"),
                                     QStringLiteral("%1 · %2 guests at %3")
                                         .arg(edited.customerName())
                                         .arg(edited.guests())
                                         .arg(startTime->time().toString(
                                             QStringLiteral("HH:mm"))),
                                     1);
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not update"), saved.error(), 3);
    }
    refresh();
}

void ReservationsPage::onCancel() {
    const int id = selectedReservationId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Nothing selected"),
                                     QStringLiteral("Choose a booking on the sheet first."), 2);
        return;
    }
    const std::vector<models::Reservation> bookings = m_ctx.reservations().onDay(m_day->date());
    const auto found = std::find_if(bookings.begin(), bookings.end(),
                                    [id](const models::Reservation& r) { return r.id() == id; });
    const QString who = found != bookings.end() ? found->customerName()
                                                : QStringLiteral("this booking");

    QMessageBox confirm(this);
    confirm.setWindowTitle(QStringLiteral("Cancel booking"));
    confirm.setIcon(QMessageBox::Warning);
    confirm.setText(QStringLiteral("Cancel %1's table?").arg(who));
    confirm.setInformativeText(QStringLiteral("The slot is released immediately and becomes "
                                              "bookable again."));
    confirm.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.button(QMessageBox::Yes)->setText(QStringLiteral("Cancel booking"));
    confirm.button(QMessageBox::Yes)->setObjectName(QStringLiteral("dangerButton"));
    confirm.button(QMessageBox::Cancel)->setText(QStringLiteral("Keep it"));
    confirm.button(QMessageBox::Cancel)->setObjectName(QStringLiteral("ghostButton"));
    if (confirm.exec() != QMessageBox::Yes) return;

    const auto cancelled = m_ctx.reservations().cancel(id);
    if (cancelled.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Booking cancelled"),
                                     QStringLiteral("%1's table is free again.").arg(who), 1);
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not cancel"), cancelled.error(), 3);
    }
    refresh();
}

void ReservationsPage::onSeat() {
    const int id = selectedReservationId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Nothing selected"),
                                     QStringLiteral("Choose a booking on the sheet first."), 2);
        return;
    }
    const auto seated = m_ctx.reservations().seat(id);
    if (seated.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Party seated"),
                                     QStringLiteral("The table is now occupied."), 1);
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not seat"), seated.error(), 3);
    }
    refresh();
}

void ReservationsPage::onComplete() {
    const int id = selectedReservationId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Nothing selected"),
                                     QStringLiteral("Choose a booking on the sheet first."), 2);
        return;
    }
    const auto completed = m_ctx.reservations().complete(id);
    if (completed.isOk()) {
        m_ctx.notifications().notify(QStringLiteral("Table turned over"),
                                     QStringLiteral("The slot is free for the next party."), 1);
    } else {
        m_ctx.notifications().notify(QStringLiteral("Could not complete"), completed.error(), 3);
    }
    refresh();
}

// ============================================================================================
//  Helpers
// ============================================================================================

int ReservationsPage::selectedReservationId() const {
    if (!m_list) return 0;
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_list->rowCount()) return 0;
    if (!m_list->selectionModel() || !m_list->selectionModel()->hasSelection()) return 0;
    const QTableWidgetItem* cell = m_list->item(row, 0);
    return cell ? cell->data(Qt::UserRole).toInt() : 0;
}

}  // namespace aluchop::gui

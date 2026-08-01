/**
 * @file MainWindow.cpp
 * @brief Implementation of the application shell.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The shell owns no business logic whatsoever. It composes the navigation rail, the nine screens,
 * a slim command bar, the toast overlay and the SPEC §10 attribution footer, and then spends the
 * rest of its life routing:
 *
 *  - Sidebar::navigate  →  QStackedWidget::setCurrentIndex  →  Page::refresh()
 *  - NotificationService::notification  →  ToastHost
 *  - NotificationService::dataChanged   →  refresh the screens that show that domain
 *  - Ctrl+Z / Ctrl+Shift+Z              →  services::CommandStack
 *  - Ctrl+T                             →  ThemeManager, persisted through SettingsService
 *  - Ctrl+K                             →  CommandPalette
 *
 * Every screen is reached through a `Page*`: the shell never knows, and never asks, which
 * concrete screen it is refreshing.
 */

#include "aluchop/gui/MainWindow.hpp"

#include "aluchop/core/AppInfo.hpp"
#include "aluchop/gui/CommandPalette.hpp"
#include "aluchop/gui/CustomersPage.hpp"
#include "aluchop/gui/DashboardPage.hpp"
#include "aluchop/gui/EmployeesPage.hpp"
#include "aluchop/gui/InventoryPage.hpp"
#include "aluchop/gui/MenuPage.hpp"
#include "aluchop/gui/OrdersPage.hpp"
#include "aluchop/gui/Page.hpp"
#include "aluchop/gui/ReportsPage.hpp"
#include "aluchop/gui/ReservationsPage.hpp"
#include "aluchop/gui/SettingsPage.hpp"
#include "aluchop/gui/Sidebar.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/gui/Toast.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/User.hpp"
#include "aluchop/services/AppContext.hpp"
#include "aluchop/services/AuthService.hpp"
#include "aluchop/services/Commands.hpp"
#include "aluchop/services/NotificationService.hpp"
#include "aluchop/services/SettingsService.hpp"

#include <QAbstractAnimation>
#include <QColor>
#include <QEasingCurve>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRectF>
#include <QShortcut>
#include <QSize>
#include <QStackedWidget>
#include <QString>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtSvg/QSvgRenderer>

#include <vector>

namespace aluchop::gui {
namespace {

/// Page indices — the single definition of "sidebar order" used by navigation, shortcuts,
/// the command palette and the data-change routing table below.
enum PageIndex {
    kDashboard = 0,
    kMenu = 1,
    kOrders = 2,
    kCustomers = 3,
    kEmployees = 4,
    kInventory = 5,
    kReservations = 6,
    kReports = 7,
    kSettings = 8
};

/// @return stroke-based SVG body for one of the command-bar glyphs.
QString barGlyphBody(const QString& key) {
    if (key == QLatin1String("search"))
        return QStringLiteral("<circle cx='10.6' cy='10.6' r='6.6'/><path d='M15.4 15.4L21 21'/>");
    if (key == QLatin1String("undo"))
        return QStringLiteral("<path d='M3.6 8.4h10.2a5.8 5.8 0 0 1 0 11.6H7.4'/>"
                              "<path d='M7.2 4.2L3.4 8.4l3.8 4.2'/>");
    if (key == QLatin1String("redo"))
        return QStringLiteral("<path d='M20.4 8.4H10.2a5.8 5.8 0 0 0 0 11.6h6.4'/>"
                              "<path d='M16.8 4.2l3.8 4.2-3.8 4.2'/>");
    if (key == QLatin1String("light"))
        return QStringLiteral("<circle cx='12' cy='12' r='4.2'/>"
                              "<path d='M12 2.4v2.6M12 19v2.6M21.6 12H19M5 12H2.4"
                              "M18.8 5.2l-1.9 1.9M7.1 16.9l-1.9 1.9M18.8 18.8l-1.9-1.9"
                              "M7.1 7.1L5.2 5.2'/>");
    if (key == QLatin1String("dark"))
        return QStringLiteral("<path d='M20.6 14.4A8.8 8.8 0 0 1 9.6 3.4a8.8 8.8 0 1 0 11 11z'/>");
    return QStringLiteral("<circle cx='12' cy='12' r='8.6'/>");
}

/// Renders a command-bar glyph; the application ships no binary icon assets by design.
QPixmap barGlyph(const QString& key, const QColor& colour, int px) {
    const qreal dpr = 2.0;
    QPixmap pm(static_cast<int>(px * dpr), static_cast<int>(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QString doc =
        QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
                       "stroke='%1' stroke-width='1.7' stroke-linecap='round' "
                       "stroke-linejoin='round'>%2</svg>")
            .arg(colour.name(QColor::HexRgb), barGlyphBody(key));

    QSvgRenderer renderer(doc.toUtf8());
    if (renderer.isValid()) {
        renderer.render(&painter, QRectF(0, 0, px, px));
    }
    return pm;
}

/// @return the screens that display @p domain, using NotificationService's exact vocabulary.
std::vector<int> screensShowing(const QString& domain) {
    if (domain == QLatin1String("menu"))         return {kDashboard, kMenu, kOrders};
    if (domain == QLatin1String("orders"))       return {kDashboard, kOrders, kCustomers, kReports};
    if (domain == QLatin1String("customers"))    return {kDashboard, kCustomers, kOrders};
    if (domain == QLatin1String("employees"))    return {kEmployees, kReports};
    if (domain == QLatin1String("inventory"))    return {kDashboard, kInventory, kReports};
    if (domain == QLatin1String("reservations")) return {kDashboard, kReservations};
    if (domain == QLatin1String("payments"))     return {kDashboard, kOrders, kReports};
    if (domain == QLatin1String("settings"))     return {kSettings};
    return {};
}

} // namespace

/// @oop-concept Object Arrays :: nine screens, nine slots — the navigation model is a fixed-size
/// std::array indexed by sidebar order, not an open-ended container
/// @oop-concept Runtime Polymorphism :: the shell holds Page* and calls pageTitle()/refresh()
/// without ever knowing which of the nine concrete screens it is talking to
MainWindow::MainWindow(services::AppContext& ctx, QWidget* parent)
    : QMainWindow(parent), m_ctx(ctx) {
    setWindowTitle(QStringLiteral("AluChop — Restaurant Management System"));
    setMinimumSize(1140, 740);
    resize(1400, 880);

    const Palette& p = ThemeManager::instance().palette();

    // --- shell frame ----------------------------------------------------------------------
    // A QFrame, not a bare QWidget, so the generated stylesheet actually paints the backdrop
    // (see the Qt gotcha documented in ThemeManager.hpp).
    auto* shell = new QFrame(this);
    shell->setObjectName(QStringLiteral("appShell"));
    shell->setFrameShape(QFrame::NoFrame);
    setCentralWidget(shell);

    auto* row = new QHBoxLayout(shell);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    m_sidebar = new Sidebar(shell);
    row->addWidget(m_sidebar);

    auto* rightColumn = new QVBoxLayout();
    rightColumn->setContentsMargins(0, 0, 0, 0);
    rightColumn->setSpacing(0);
    row->addLayout(rightColumn, 1);

    // --- command bar: global search, undo/redo, theme, signed-in user ------------------------
    auto* bar = new QFrame(shell);
    bar->setObjectName(QStringLiteral("commandBar"));
    bar->setFrameShape(QFrame::NoFrame);
    bar->setFixedHeight(66);

    auto* barRow = new QHBoxLayout(bar);
    barRow->setContentsMargins(24, 14, 24, 14);
    barRow->setSpacing(10);

    auto* search = new QPushButton(QStringLiteral("   Search everywhere…"), bar);
    search->setObjectName(QStringLiteral("searchBar"));
    search->setCursor(Qt::PointingHandCursor);
    search->setFlat(false);
    search->setMinimumWidth(340);
    search->setMinimumHeight(38);
    search->setIcon(QIcon(barGlyph(QStringLiteral("search"), p.textMuted, 16)));
    search->setIconSize(QSize(16, 16));
    search->setToolTip(QStringLiteral("Search everywhere  (Ctrl+K)"));
    barRow->addWidget(search, 0, Qt::AlignVCenter);
    barRow->addStretch(1);

    const auto makeToolButton = [bar, &p](const QString& key, const QString& tip) {
        auto* button = new QToolButton(bar);
        button->setCursor(Qt::PointingHandCursor);
        button->setIcon(QIcon(barGlyph(key, p.textMuted, 18)));
        button->setIconSize(QSize(18, 18));
        button->setToolTip(tip);
        button->setProperty("glyphKey", key);
        button->setFixedSize(36, 36);
        return button;
    };

    auto* undoButton = makeToolButton(QStringLiteral("undo"), QStringLiteral("Undo  (Ctrl+Z)"));
    auto* redoButton = makeToolButton(QStringLiteral("redo"), QStringLiteral("Redo  (Ctrl+Shift+Z)"));
    auto* themeButton = makeToolButton(
        ThemeManager::instance().mode() == ThemeManager::Mode::Light ? QStringLiteral("dark")
                                                                     : QStringLiteral("light"),
        QStringLiteral("Switch theme  (Ctrl+T)"));

    barRow->addWidget(undoButton);
    barRow->addWidget(redoButton);
    barRow->addWidget(themeButton);

    auto* divider = new QFrame(bar);
    divider->setFrameShape(QFrame::VLine);
    divider->setFixedWidth(1);
    divider->setFixedHeight(24);
    barRow->addSpacing(6);
    barRow->addWidget(divider);
    barRow->addSpacing(6);

    auto* who = new QLabel(bar);
    who->setObjectName(QStringLiteral("mutedLabel"));
    who->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (const auto& user = m_ctx.auth().currentUser()) {
        who->setText(QStringLiteral("%1  ·  %2")
                         .arg(user->username(), models::toString(user->role())));
    } else {
        who->setText(QStringLiteral("Not signed in"));
    }
    barRow->addWidget(who, 0, Qt::AlignVCenter);

    rightColumn->addWidget(bar);

    auto* barRule = new QFrame(shell);
    barRule->setFrameShape(QFrame::HLine);
    barRule->setFixedHeight(1);
    rightColumn->addWidget(barRule);

    // --- the nine screens ---------------------------------------------------------------------
    m_stack = new QStackedWidget(shell);
    m_stack->setObjectName(QStringLiteral("pageStack"));
    rightColumn->addWidget(m_stack, 1);

    m_pages[kDashboard]    = new DashboardPage(m_ctx);
    m_pages[kMenu]         = new MenuPage(m_ctx);
    m_pages[kOrders]       = new OrdersPage(m_ctx);
    m_pages[kCustomers]    = new CustomersPage(m_ctx);
    m_pages[kEmployees]    = new EmployeesPage(m_ctx);
    m_pages[kInventory]    = new InventoryPage(m_ctx);
    m_pages[kReservations] = new ReservationsPage(m_ctx);
    m_pages[kReports]      = new ReportsPage(m_ctx);
    m_pages[kSettings]     = new SettingsPage(m_ctx);

    // addWidget() reparents each page, so Qt owns them from here on (ARCHITECTURE §9 rule 1).
    for (Page* page : m_pages) {
        m_stack->addWidget(page);
    }

    // --- navigation rail entries — order defines the page index ---------------------------------
    m_sidebar->addEntry(QStringLiteral("assets/icons/dashboard.svg"), QStringLiteral("Dashboard"));
    m_sidebar->addEntry(QStringLiteral("assets/icons/menu.svg"), QStringLiteral("Menu"));
    m_sidebar->addEntry(QStringLiteral("assets/icons/orders.svg"), QStringLiteral("Orders"));
    m_sidebar->addEntry(QStringLiteral("assets/icons/customers.svg"), QStringLiteral("Customers"));
    m_sidebar->addEntry(QStringLiteral("assets/icons/employees.svg"), QStringLiteral("Employees"));
    m_sidebar->addEntry(QStringLiteral("assets/icons/inventory.svg"), QStringLiteral("Inventory"));
    m_sidebar->addEntry(QStringLiteral("assets/icons/reservations.svg"),
                        QStringLiteral("Reservations"));
    m_sidebar->addEntry(QStringLiteral("assets/icons/reports.svg"), QStringLiteral("Reports"));
    m_sidebar->addEntry(QStringLiteral("assets/icons/settings.svg"), QStringLiteral("Settings"));

    // --- footer, toasts, palette ----------------------------------------------------------------
    buildFooter();
    if (m_footer) {
        auto* footRule = new QFrame(shell);
        footRule->setFrameShape(QFrame::HLine);
        footRule->setFixedHeight(1);
        rightColumn->addWidget(footRule);
        rightColumn->addWidget(m_footer);
    }

    m_toasts = new ToastHost(this);
    m_palette = new CommandPalette(m_ctx, this);

    // --- wiring ----------------------------------------------------------------------------------
    connect(m_sidebar, &Sidebar::navigate, this, &MainWindow::onNavigate);
    connect(search, &QPushButton::clicked, this, &MainWindow::onOpenPalette);
    connect(undoButton, &QToolButton::clicked, this, &MainWindow::onUndo);
    connect(redoButton, &QToolButton::clicked, this, &MainWindow::onRedo);
    connect(themeButton, &QToolButton::clicked, this, &MainWindow::onToggleTheme);

    connect(m_palette, &CommandPalette::navigateRequested, this, &MainWindow::onNavigate);
    connect(m_palette, &CommandPalette::openOrderRequested, this, [this](int orderId) {
        onNavigate(kOrders);
        m_toasts->show(QStringLiteral("Orders"),
                       QStringLiteral("Opened the orders board — select order #%1 to edit it.")
                           .arg(orderId),
                       0);
    });

    // Connections to the service bus use `this` as the context object, so they are severed
    // automatically when the window dies (ARCHITECTURE §9 rule 5).
    connect(&m_ctx.notifications(), &services::NotificationService::notification, this,
            &MainWindow::onNotification);
    connect(&m_ctx.notifications(), &services::NotificationService::dataChanged, this,
            &MainWindow::onDataChanged);

    // Generated pixmaps are raster, so they need re-tinting whenever the palette changes.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [search, undoButton, redoButton, themeButton]() {
                const Palette& live = ThemeManager::instance().palette();
                const bool lightNow = ThemeManager::instance().mode() == ThemeManager::Mode::Light;
                search->setIcon(QIcon(barGlyph(QStringLiteral("search"), live.textMuted, 16)));
                undoButton->setIcon(QIcon(barGlyph(QStringLiteral("undo"), live.textMuted, 18)));
                redoButton->setIcon(QIcon(barGlyph(QStringLiteral("redo"), live.textMuted, 18)));
                themeButton->setIcon(QIcon(barGlyph(
                    lightNow ? QStringLiteral("dark") : QStringLiteral("light"),
                    live.textMuted, 18)));
            });

    buildShortcuts();

    // Land on the dashboard, fully populated.
    onNavigate(kDashboard);
}

void MainWindow::buildShortcuts() {
    // Every shortcut is parented to this window, so the whole keyboard map dies with the shell.
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this, this, &MainWindow::onOpenPalette);
    new QShortcut(QKeySequence::Undo, this, this, &MainWindow::onUndo);
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")), this, this, &MainWindow::onRedo);
    new QShortcut(QKeySequence::Redo, this, this, &MainWindow::onRedo);
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+T")), this, this, &MainWindow::onToggleTheme);

    // Ctrl+1 … Ctrl+9 jump straight to a screen, in sidebar order.
    for (int i = 0; i < static_cast<int>(m_pages.size()); ++i) {
        const QKeySequence jump(QStringLiteral("Ctrl+%1").arg(i + 1));
        new QShortcut(jump, this, this, [this, i]() { onNavigate(i); });
    }

    // Ctrl+N — go to Orders and start one. OrdersPage exposes onNewOrder() as a *public* slot
    // precisely so the shell can bind this without reaching into the screen's internals.
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+N")), this, this, [this]() {
        onNavigate(kOrders);
        if (auto* orders = qobject_cast<OrdersPage*>(m_pages[kOrders])) {
            orders->onNewOrder();
        }
    });

    // F5 — re-query the visible screen.
    new QShortcut(QKeySequence(Qt::Key_F5), this, this, [this]() {
        const int current = m_stack->currentIndex();
        if (current >= 0 && current < static_cast<int>(m_pages.size()) && m_pages[current]) {
            m_pages[current]->refresh();
            m_toasts->show(QStringLiteral("Refreshed"), m_pages[current]->pageTitle(), 0, 1600);
        }
    });
}

void MainWindow::buildFooter() {
    // SPEC §10, sourced from core::kAppInfo so the credit exists in exactly one place.
    auto* footer = new QLabel(centralWidget());
    footer->setObjectName(QStringLiteral("footerCredit"));
    footer->setAlignment(Qt::AlignCenter);
    footer->setTextFormat(Qt::RichText);
    footer->setContentsMargins(0, 9, 0, 9);
    footer->setText(
        QStringLiteral("Designed &amp; Developed by <b>%1</b> &nbsp;·&nbsp; %2 "
                       "&nbsp;·&nbsp; %3 &nbsp;·&nbsp; © 2026 %4 v%5")
            .arg(QString::fromUtf8(core::kAppInfo.developer),
                 QString::fromUtf8(core::kAppInfo.rollNo),
                 QString::fromUtf8(core::kAppInfo.email),
                 QString::fromUtf8(core::kAppInfo.appName),
                 QString::fromUtf8(core::kAppInfo.version)));
    footer->setToolTip(QStringLiteral(
        "© 2026 AluChop Restaurant Management System. Developed by Shashank Bhattarai "
        "(ACE082BCT078). For academic use as an ENCT151 Object-Oriented Programming "
        "coursework project. All rights reserved."));
    m_footer = footer;
}

void MainWindow::onNavigate(int pageIndex) {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()) || !m_pages[pageIndex]) {
        return;
    }

    Page* page = m_pages[pageIndex];
    m_stack->setCurrentIndex(pageIndex);
    m_sidebar->setActive(pageIndex);
    setWindowTitle(QStringLiteral("AluChop — %1").arg(page->pageTitle()));

    // Screens are contractually idempotent and must never throw, so a plain call is correct here.
    page->refresh();

    // A short cross-fade makes navigation read as one application rather than nine. The effect is
    // installed only when the screen has none of its own, and is removed again once the fade is
    // over so no page pays the cost of an offscreen render buffer while idle.
    if (!page->graphicsEffect()) {
        auto* fade = new QGraphicsOpacityEffect(page);
        fade->setOpacity(0.0);
        page->setGraphicsEffect(fade);

        auto* anim = new QPropertyAnimation(fade, "opacity", page);
        anim->setDuration(180);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::InOutQuad);
        connect(anim, &QPropertyAnimation::finished, page, [page]() {
            // Deferred: never destroy the effect from inside the animation's own callback.
            QTimer::singleShot(0, page, [page]() { page->setGraphicsEffect(nullptr); });
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void MainWindow::onDataChanged(const QString& domain) {
    const int current = m_stack->currentIndex();
    if (current < 0 || current >= static_cast<int>(m_pages.size()) || !m_pages[current]) {
        return;
    }

    // Only the screen the user is actually looking at is re-queried; the others re-read their
    // services the moment they are navigated to, which keeps a busy service shift from running
    // nine refreshes per change.
    for (int index : screensShowing(domain)) {
        if (index == current) {
            m_pages[current]->refresh();
            return;
        }
    }
}

void MainWindow::onNotification(const QString& title, const QString& message, int level) {
    if (m_toasts) {
        m_toasts->show(title, message, level);
    }
}

void MainWindow::onUndo() {
    services::CommandStack& stack = m_ctx.commands();
    if (!stack.canUndo()) {
        m_toasts->show(QStringLiteral("Nothing to undo"),
                       QStringLiteral("No reversible edit has been made yet."), 0, 2200);
        return;
    }

    // Capture the description first — after the undo it has moved to the redo stack.
    const QString what = stack.undoText();
    const core::Result<void> result = stack.undo();

    if (!result) {
        m_toasts->show(QStringLiteral("Could not undo"), result.error(), 3);
        return;
    }
    m_toasts->show(QStringLiteral("Undone"), what, 1);

    const int current = m_stack->currentIndex();
    if (current >= 0 && m_pages[current]) {
        m_pages[current]->refresh();
    }
}

void MainWindow::onRedo() {
    services::CommandStack& stack = m_ctx.commands();
    if (!stack.canRedo()) {
        m_toasts->show(QStringLiteral("Nothing to redo"),
                       QStringLiteral("There is no undone edit waiting to be replayed."), 0, 2200);
        return;
    }

    const QString what = stack.redoText();
    const core::Result<void> result = stack.redo();

    if (!result) {
        m_toasts->show(QStringLiteral("Could not redo"), result.error(), 3);
        return;
    }
    m_toasts->show(QStringLiteral("Redone"), what, 1);

    const int current = m_stack->currentIndex();
    if (current >= 0 && m_pages[current]) {
        m_pages[current]->refresh();
    }
}

void MainWindow::onToggleTheme() {
    ThemeManager& theme = ThemeManager::instance();
    theme.toggle();

    const bool dark = (theme.mode() == ThemeManager::Mode::Dark);

    // The preference is part of the restaurant's settings, not a GUI global, so it is written
    // through the service that owns the settings table.
    m_ctx.settings().set(QStringLiteral("theme.mode"),
                         dark ? QStringLiteral("dark") : QStringLiteral("light"));

    m_toasts->show(dark ? QStringLiteral("Dark mode") : QStringLiteral("Light mode"),
                   QStringLiteral("Theme saved — it will be remembered next time."), 1, 2200);
}

void MainWindow::onOpenPalette() {
    if (!m_palette) {
        return;
    }
    m_palette->show();
    m_palette->raise();
    m_palette->activateWindow();
}

} // namespace aluchop::gui

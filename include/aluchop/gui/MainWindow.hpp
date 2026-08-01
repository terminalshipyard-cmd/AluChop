#pragma once

/**
 * @file MainWindow.hpp
 * @brief Application shell: sidebar, stacked screens, toasts, shortcuts, attribution footer.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QMainWindow>
#include <QString>

#include <array>

namespace aluchop::services {
class AppContext;
}

class QLabel;
class QStackedWidget;

namespace aluchop::gui {

class CommandPalette;
class Page;
class Sidebar;
class ToastHost;

/**
 * @brief The one window the user lives in after signing in.
 *
 * Layout: a Sidebar rail on the left, a QStackedWidget of the nine Page screens on the right,
 * a ToastHost overlay pinned to the top-right corner for notifications, and the SPEC §10
 * attribution footer along the bottom.
 *
 * The shell owns no business logic. It:
 *  - routes navigation (Sidebar::navigate → QStackedWidget::setCurrentIndex → Page::refresh),
 *  - listens to services::NotificationService for @c notification (→ toast) and
 *    @c dataChanged (→ refresh the affected screen only),
 *  - drives services::CommandStack for global Undo/Redo,
 *  - flips ThemeManager between Light and Dark,
 *  - opens the CommandPalette for global search.
 *
 * @par Keyboard shortcuts (built in buildShortcuts())
 * | Shortcut        | Action                                   |
 * |-----------------|------------------------------------------|
 * | Ctrl+K          | Global search / command palette          |
 * | Ctrl+Z          | Undo the last undoable edit              |
 * | Ctrl+Shift+Z    | Redo                                     |
 * | Ctrl+T          | Toggle Light/Dark theme                  |
 * | Ctrl+1 … Ctrl+9 | Jump to screen 1..9 (sidebar order)      |
 * | Ctrl+N          | New order (jumps to Orders and starts one)|
 * | F5              | Refresh the current screen               |
 *
 * @par Ownership
 * Heap-allocated with no parent and @c Qt::WA_DeleteOnClose by main.cpp (ARCHITECTURE §9
 * rule 2). Every child — sidebar, stack, pages, footer, toast host, palette — is Qt-parented
 * and destroyed with the window. The AppContext reference outlives it: `ctx` is destroyed
 * only after `app.exec()` returns.
 *
 * @oop-concept Object Arrays :: the nine screens live in a fixed std::array<Page*, 9> because
 *              the navigation model is exactly nine slots, indexed by sidebar order
 * @oop-concept Runtime Polymorphism :: the shell talks to every screen through Page* only —
 *              it never knows which concrete screen it is refreshing
 */
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    /// @param ctx composition root; every screen receives the same reference.
    /// @param parent normally null — this is the top-level window.
    MainWindow(services::AppContext& ctx, QWidget* parent = nullptr);

    ~MainWindow() override = default;

private slots:
    /// Switches the stacked widget, syncs the sidebar highlight, updates the window title and
    /// calls refresh() on the newly shown screen.
    /// @param pageIndex 0..8, sidebar order.
    void onNavigate(int pageIndex);

    /// Refreshes the current screen when a domain it displays has changed.
    /// @param domain one of "menu", "orders", "customers", "employees", "inventory",
    ///               "reservations", "payments", "settings" (NotificationService contract).
    void onDataChanged(const QString& domain);

    /// Renders a service notification as a stacked toast.
    /// @param title short headline.
    /// @param message body text.
    /// @param level models::NoticeLevel as int: 0 Info, 1 Success, 2 Warning, 3 Danger.
    void onNotification(const QString& title, const QString& message, int level);

    /// Ctrl+Z — pops services::CommandStack, toasts the undone description or the error.
    void onUndo();

    /// Ctrl+Shift+Z — replays the last undone command.
    void onRedo();

    /// Ctrl+T — flips ThemeManager and persists the choice through SettingsService.
    void onToggleTheme();

    /// Ctrl+K — shows the CommandPalette centred over the window.
    void onOpenPalette();

private:
    /// Installs every QShortcut listed in the class documentation. Shortcuts are parented to
    /// this window, so they die with it.
    void buildShortcuts();

    /// Builds the subtle centred credit line required by SPEC §10:
    /// "Designed & Developed by / Shashank Bhattarai / ACE082BCT078 /
    /// shashankbhattarai006@gmail.com", sourced from core::kAppInfo so the text exists once.
    void buildFooter();

    services::AppContext& m_ctx;          ///< Composition root (outlives this window).
    Sidebar* m_sidebar = nullptr;         ///< Left navigation rail.
    QStackedWidget* m_stack = nullptr;    ///< Screen container.
    std::array<Page*, 9> m_pages{};       ///< Dashboard…Settings, index == sidebar order.
    QLabel* m_footer = nullptr;           ///< SPEC §10 attribution line (objectName "footerCredit").
    ToastHost* m_toasts = nullptr;        ///< Notification overlay.
    CommandPalette* m_palette = nullptr;  ///< Ctrl+K global search dialog.
};

} // namespace aluchop::gui

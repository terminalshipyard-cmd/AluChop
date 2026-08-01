#pragma once

/**
 * @file CommandPalette.hpp
 * @brief Ctrl+K global search across screens, menu items, customers and orders.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QDialog>
#include <QString>

namespace aluchop::services {
class AppContext;
}

class QLineEdit;
class QListWidget;

namespace aluchop::gui {

/**
 * @brief "Search everywhere" — one field that reaches the entire application.
 *
 * A frameless, glassmorphic dialog centred over MainWindow. Each keystroke merges results
 * from four sources into one ranked list:
 *  - the nine screen names (navigation),
 *  - services::MenuService::search() (dishes),
 *  - services::CustomerService::search() (people),
 *  - order-number prefix matches through services::OrderService.
 *
 * The palette performs no action itself: it emits navigateRequested() or openOrderRequested()
 * and closes, leaving MainWindow to decide what happens. That keeps the search UI free of any
 * business rule and makes it safe to open from anywhere.
 *
 * @par Ownership
 * `new CommandPalette(ctx, mainWindow)` — parented to the window, reused across invocations
 * rather than rebuilt, so its result list stays warm.
 *
 * @oop-concept Polymorphic Result Handling :: heterogeneous hits (page, dish, customer, order)
 *              share one list, each row tagged with its kind via Qt::UserRole item data
 */
class CommandPalette : public QDialog {
    Q_OBJECT
public:
    /// @param ctx composition root; uses menu(), customers() and orders() read-only.
    /// @param parent the MainWindow the palette floats over.
    explicit CommandPalette(services::AppContext& ctx, QWidget* parent = nullptr);

signals:
    /// The user picked a screen.
    /// @param pageIndex 0..8 in sidebar order.
    void navigateRequested(int pageIndex);

    /// The user picked an order.
    /// @param orderId id of the order to open on the Orders screen.
    void openOrderRequested(int orderId);

private slots:
    /// Rebuilds the merged result list for @p q. An empty query lists the nine screens.
    /// @param q the current search text.
    void onQueryChanged(const QString& q);

    /// Emits the signal matching the highlighted row's kind, then accepts the dialog.
    void onActivated();

private:
    services::AppContext& m_ctx;      ///< Composition root.
    QLineEdit* m_input = nullptr;     ///< Query field (objectName "paletteInput").
    QListWidget* m_results = nullptr; ///< Merged, grouped hits (objectName "paletteResults").
};

} // namespace aluchop::gui

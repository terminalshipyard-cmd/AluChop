#pragma once

/**
 * @file Page.hpp
 * @brief Abstract base of the nine navigable screens hosted by MainWindow.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QString>
#include <QWidget>

namespace aluchop::services {
class AppContext;
}

namespace aluchop::gui {

/**
 * @brief Abstract screen: everything MainWindow's QStackedWidget is allowed to contain.
 *
 * A Page is the presentation half of one business domain. It receives the composition root
 * (services::AppContext) **by reference** through its constructor — no page ever reaches for
 * a global service, and no page contains SQL: it only calls services, which call repositories.
 *
 * MainWindow stores `std::array<Page*, 9>` and drives every screen through this interface:
 * pageTitle() for the header, refresh() whenever the underlying domain announces a change.
 *
 * @par Ownership
 * Pages are heap-allocated with @c new and immediately handed to the QStackedWidget
 * (`stack->addWidget(page)`), which reparents them; Qt deletes them with MainWindow.
 * Never wrap a Page in a smart pointer (ARCHITECTURE §9 rule 1).
 *
 * @oop-concept Abstract Class :: Page cannot exist on its own — a screen without a title or a
 *              refresh rule is meaningless, so both are pure virtual
 * @oop-concept Hierarchical Inheritance (GUI) :: nine concrete pages specialise one abstract Page
 * @oop-concept Runtime Polymorphism :: MainWindow refreshes/labels screens through Page* alone
 */
class Page : public QWidget {
    Q_OBJECT
public:
    /// @param ctx composition root, guaranteed to outlive every window (ARCHITECTURE §9 rule 4).
    /// @param parent Qt parent; normally left null because QStackedWidget::addWidget reparents.
    explicit Page(services::AppContext& ctx, QWidget* parent = nullptr);

    /// Virtual through QWidget — deleting a Page* deletes the concrete screen correctly.
    ~Page() override = default;

    /// @oop-concept Pure Virtual Functions :: the title is screen identity, only the screen knows it
    /// @return the human-readable screen name shown in the header and command palette.
    virtual QString pageTitle() const = 0;

    /// Re-queries its services and repopulates every view it owns.
    /// Called on first show, on F5, and whenever NotificationService::dataChanged names a
    /// domain this screen displays. Must be idempotent and must never throw.
    virtual void refresh() = 0;

protected:
    /// @oop-concept Pass by Reference :: the service graph is wired, never copied
    services::AppContext& m_ctx;  ///< Non-owning reference to the composition root.
};

} // namespace aluchop::gui

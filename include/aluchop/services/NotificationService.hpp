#pragma once

/**
 * @file NotificationService.hpp
 * @brief The application's event bus: services announce, the GUI listens.
 *
 * This is deliberately the **only** `QObject` in the service layer. Because everything else talks
 * to it by reference instead of inheriting `QObject`, the rest of the business logic stays free of
 * moc, free of signal/slot plumbing and trivially unit-testable — while the GUI still gets live
 * toasts and automatic table refreshes.
 */

#include <QObject>
#include <QString>

namespace aluchop::services {

/**
 * @brief Broadcasts user-facing notices and "this domain changed" hints.
 *
 * The level parameter is `models::NoticeLevel` widened to `int` because Qt's queued-signal
 * machinery is happiest with registered/primitive types, and the GUI narrows it straight back.
 */
class NotificationService : public QObject {
    Q_OBJECT
public:
    /// @oop-concept Default Arguments :: a parentless bus is the normal case (AppContext owns it)
    explicit NotificationService(QObject* parent = nullptr);

    /**
     * @brief Emits a user-facing notice (rendered as a toast).
     * @param level 0 Info, 1 Success, 2 Warning, 3 Danger — see `models::NoticeLevel`.
     */
    void notify(const QString& title, const QString& message, int level = 0);

    /**
     * @brief Tells every open screen that a domain's data moved underneath it.
     * @param domain exactly one of: `menu`, `orders`, `customers`, `employees`, `inventory`,
     *        `reservations`, `payments`, `settings`.
     */
    void announceDataChanged(const QString& domain);

signals:
    /// Raised by notify(); the shell shows a toast.
    void notification(const QString& title, const QString& message, int level);

    /// Raised by announceDataChanged(); pages matching @p domain call refresh().
    void dataChanged(const QString& domain);
};

} // namespace aluchop::services

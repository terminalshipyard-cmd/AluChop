#pragma once

/**
 * @file Toast.hpp
 * @brief Stacked, auto-dismissing notification popups (SPEC §3 "Notifications").
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QFrame>
#include <QString>
#include <QWidget>

#include <cstddef>
#include <vector>

class QLabel;

namespace aluchop::gui {

/**
 * @brief A single notification card: coloured accent bar, title, message, fade animations.
 *
 * Levels mirror models::NoticeLevel (passed across the signal boundary as int, as
 * services::NotificationService declares): 0 Info, 1 Success, 2 Warning, 3 Danger. The level
 * is exposed to the stylesheet as the dynamic property @c "level", so ThemeManager can tint
 * the accent bar with Palette::primary / success / accent / danger without any hand-painting.
 *
 * A toast fades in on construction-time popIn(), starts a single-shot QTimer and fades itself
 * out, then emits dismissed() so its host can drop it from the stack and delete it safely.
 *
 * @par Ownership
 * Parented to the ToastHost; deletion happens via QObject::deleteLater() after the fade-out
 * so no widget is destroyed inside its own animation callback.
 *
 * @oop-concept Single Inheritance :: a toast is a QFrame with a lifetime rule bolted on
 */
class Toast : public QFrame {
    Q_OBJECT
public:
    /// @param title short headline, e.g. "Low stock".
    /// @param message body text, e.g. "Paneer is below its threshold (2.0 kg)."
    /// @param level 0 Info, 1 Success, 2 Warning, 3 Danger.
    /// @param parent the ToastHost that lays this toast out.
    Toast(const QString& title, const QString& message, int level, QWidget* parent);

    /// Fades in (QGraphicsOpacityEffect + QPropertyAnimation) and arms the auto-dismiss timer.
    /// @param lifetimeMs visible time before the fade-out starts.
    void popIn(int lifetimeMs);

    /// Fades out immediately and then emits dismissed(). Called by the timer or by a click.
    void dismiss();

    /// @return the notification level this toast was built with.
    int level() const noexcept { return m_level; }

signals:
    /// Emitted after the fade-out completes; the host removes and deletes this toast.
    /// @param self the toast that finished — lets the host find it without a lookup.
    void dismissed(Toast* self);

private:
    QLabel* m_title = nullptr;    ///< Headline label (objectName "toastTitle").
    QLabel* m_message = nullptr;  ///< Body label (objectName "toastMessage").
    int m_level = 0;              ///< Notice level, also exposed as a QSS dynamic property.
};

/**
 * @brief Transparent overlay pinned to MainWindow's top-right corner that stacks toasts.
 *
 * MainWindow connects services::NotificationService::notification to its own slot and forwards
 * to show(). The host keeps at most four toasts alive; a fifth dismisses the oldest early.
 *
 * @par Styling note
 * ToastHost is a bare QWidget by architectural contract and is intentionally transparent
 * (@c Qt::WA_TransparentForMouseEvents is *not* set — toasts must stay clickable, but the
 * empty area of the host is; the host paints nothing itself, so the QSS-background gotcha
 * described in ThemeManager.hpp does not apply to it).
 *
 * @par Ownership
 * `new ToastHost(mainWindow)` — parented, deleted with the window. It owns its Toast children
 * through the normal Qt parent/child chain.
 */
class ToastHost : public QWidget {
    Q_OBJECT
public:
    /// @param parent the widget whose top-right corner the host tracks; must not be null.
    explicit ToastHost(QWidget* parent);

    /// The base-class overloads (`show()`, `hide()` …) stay reachable next to the notification
    /// overload below — without this using-declaration the four-argument show() would hide them.
    using QWidget::show;

    /// Pushes a new notification onto the stack.
    /// @param title short headline.
    /// @param message body text.
    /// @param level 0 Info, 1 Success, 2 Warning, 3 Danger.
    /// @param ms visible time before auto-dismissal, in milliseconds.
    ///
    /// @oop-concept Function Overloading :: `show(title, message, level)` lives beside the
    ///              inherited `QWidget::show()`; same verb, different jobs, resolved by arity
    /// @oop-concept Default Arguments :: callers rarely care about the lifetime
    void show(const QString& title, const QString& message, int level, int ms = 3500);

private slots:
    /// Removes a finished toast from the stack, re-lays out the rest and deletes it later.
    /// @param toast the toast that emitted Toast::dismissed.
    void onToastDismissed(Toast* toast);

private:
    /// Re-stacks the live toasts bottom-up and resizes/repositions the host over its parent.
    void relayout();

    std::vector<Toast*> m_toasts;                 ///< Live toasts, oldest first; Qt owns them.
    static constexpr std::size_t kMaxVisible = 4; ///< @oop-concept Constants :: stack depth is a rule, not a magic number
};

} // namespace aluchop::gui

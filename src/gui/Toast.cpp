/**
 * @file Toast.cpp
 * @brief Implementation of the stacked, auto-dismissing notification popups.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * services::NotificationService announces; MainWindow forwards; this file is where an announcement
 * becomes something the user actually sees. A Toast is a small themed card that fades itself in,
 * waits, fades itself out and then asks its host to forget it — the whole lifetime is self-driven,
 * so no caller ever has to remember to delete one.
 *
 * The host stacks at most four of them in the window's top-right corner and slides the survivors
 * into their new slots whenever one leaves, which is what makes a burst of notifications read as a
 * queue rather than as flicker.
 */

#include "aluchop/gui/Toast.hpp"

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QObject>
#include <QPoint>
#include <QPropertyAnimation>
#include <QSize>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>

namespace aluchop::gui {
namespace {

/// Forwards the events of a watched widget to a callback.
///
/// Neither Toast nor ToastHost declares an `eventFilter`, `resizeEvent` or `mousePressEvent`
/// override in its frozen header, yet both need to react to events: the host must follow its
/// parent's corner, and a toast must dismiss itself when clicked. Rather than alter the sealed
/// contract, both install this tiny helper as an event filter. It is a plain QObject with no
/// signals or slots, so it needs no meta-object and no moc pass.
///
/// @oop-concept Single Inheritance :: a purpose-built QObject that adds exactly one behaviour
/// @oop-concept Method Overriding :: QObject::eventFilter is overridden to divert events
class EventHook : public QObject {
public:
    /// @param owner parent that will delete this hook.
    /// @param onEvent invoked for every event delivered to the watched object.
    EventHook(QObject* owner, std::function<void(QEvent*)> onEvent)
        : QObject(owner), m_onEvent(std::move(onEvent)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        m_onEvent(event);                              // observe only — never swallow
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void(QEvent*)> m_onEvent;
};

constexpr int kToastWidth = 340;   ///< Fixed card width — a notification is never a paragraph.
constexpr int kEdgeMargin = 22;    ///< Distance from the window's top and right edges.
constexpr int kGap = 10;           ///< Vertical gap between stacked cards.

} // namespace

// =============================================================================================
// Toast
// =============================================================================================

Toast::Toast(const QString& title, const QString& message, int level, QWidget* parent)
    : QFrame(parent), m_level(level) {
    setObjectName(QStringLiteral("toast"));
    setFrameShape(QFrame::NoFrame);
    setFixedWidth(kToastWidth);
    setCursor(Qt::PointingHandCursor);

    // The level is handed to the stylesheet as a dynamic property so ThemeManager's
    // `#toast[level="3"]` rule paints the accent bar — no colour is decided in this file.
    setProperty("level", level);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 13, 16, 13);
    column->setSpacing(3);

    m_title = new QLabel(title, this);
    m_title->setObjectName(QStringLiteral("toastTitle"));
    m_title->setWordWrap(true);

    m_message = new QLabel(message, this);
    m_message->setObjectName(QStringLiteral("toastMessage"));
    m_message->setWordWrap(true);
    m_message->setVisible(!message.isEmpty());

    column->addWidget(m_title);
    column->addWidget(m_message);

    // A click anywhere on the card dismisses it early — see EventHook for why this is a filter
    // rather than a mousePressEvent override.
    installEventFilter(new EventHook(this, [this](QEvent* event) {
        if (event->type() == QEvent::MouseButtonRelease) {
            dismiss();
        }
    }));

    adjustSize();
}

void Toast::popIn(int lifetimeMs) {
    auto* fade = new QGraphicsOpacityEffect(this);
    fade->setOpacity(0.0);
    setGraphicsEffect(fade);

    auto* anim = new QPropertyAnimation(fade, "opacity", this);
    anim->setDuration(220);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::InOutQuad);
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    show();

    // Auto-dismissal. A lifetime of zero (or less) means "stay until something else removes it",
    // which the host uses when it evicts the oldest card early.
    if (lifetimeMs > 0) {
        QTimer::singleShot(lifetimeMs, this, &Toast::dismiss);
    }
}

void Toast::dismiss() {
    // Both the timer and a user click can arrive; the second one must not start a second fade or
    // emit dismissed() twice. The guard is a dynamic property because the frozen header has no
    // member for it.
    if (property("dismissing").toBool()) {
        return;
    }
    setProperty("dismissing", true);

    auto* fade = qobject_cast<QGraphicsOpacityEffect*>(graphicsEffect());
    if (!fade) {
        fade = new QGraphicsOpacityEffect(this);
        fade->setOpacity(1.0);
        setGraphicsEffect(fade);
    }

    auto* anim = new QPropertyAnimation(fade, "opacity", this);
    anim->setDuration(200);
    anim->setStartValue(fade->opacity());
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::InOutQuad);

    // dismissed() is emitted only once the card is invisible, so the host never deletes a widget
    // that is still mid-animation.
    connect(anim, &QPropertyAnimation::finished, this, [this]() { emit dismissed(this); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// =============================================================================================
// ToastHost
// =============================================================================================

ToastHost::ToastHost(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("toastHost"));

    // The host itself is nothing but empty space around the cards, so clicks pass straight
    // through it to the screen underneath. The attribute is per-widget: the Toast children stay
    // fully interactive.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);

    if (parent) {
        // A lambda written inside a member function keeps this class's access rights, which is
        // how the watcher can reach the private relayout().
        parent->installEventFilter(new EventHook(this, [this](QEvent* event) {
            const QEvent::Type t = event->type();
            if (t == QEvent::Resize || t == QEvent::Move || t == QEvent::Show) {
                relayout();
            }
        }));
    }
    hide();
}

/// @oop-concept Function Overloading :: `show(title, message, level, ms)` sits beside the
/// inherited `QWidget::show()`; same verb, different job, resolved by arity
void ToastHost::show(const QString& title, const QString& message, int level, int ms) {
    // A fifth notification pushes the oldest one out early rather than growing the stack.
    while (m_toasts.size() >= kMaxVisible) {
        Toast* oldest = m_toasts.front();
        m_toasts.erase(m_toasts.begin());
        disconnect(oldest, nullptr, this, nullptr);
        oldest->hide();
        oldest->deleteLater();
    }

    auto* toast = new Toast(title, message, level, this);
    connect(toast, &Toast::dismissed, this, &ToastHost::onToastDismissed);
    m_toasts.push_back(toast);

    relayout();
    QWidget::show();
    raise();

    toast->popIn(ms);
}

void ToastHost::onToastDismissed(Toast* toast) {
    const auto it = std::find(m_toasts.begin(), m_toasts.end(), toast);
    if (it != m_toasts.end()) {
        m_toasts.erase(it);
    }
    toast->hide();
    toast->deleteLater();   // never destroy a widget from inside its own animation callback

    relayout();
    if (m_toasts.empty()) {
        QWidget::hide();
    }
}

void ToastHost::relayout() {
    QWidget* host = parentWidget();
    if (!host) {
        return;
    }

    // Measure first: cards word-wrap, so their heights differ.
    int total = 0;
    for (Toast* toast : m_toasts) {
        toast->setFixedWidth(kToastWidth);
        toast->adjustSize();
        total += toast->height();
    }
    if (!m_toasts.empty()) {
        total += static_cast<int>(m_toasts.size() - 1) * kGap;
    }

    setGeometry(host->width() - kToastWidth - kEdgeMargin, kEdgeMargin, kToastWidth,
                std::max(total, 1));

    // Oldest at the top, newest sliding in underneath. Positions are animated so that removing a
    // card from the middle of the stack closes the gap smoothly instead of snapping.
    int y = 0;
    for (Toast* toast : m_toasts) {
        const QPoint target(0, y);
        if (toast->isVisible() && toast->pos() != target) {
            auto* slide = new QPropertyAnimation(toast, "pos", toast);
            slide->setDuration(180);
            slide->setStartValue(toast->pos());
            slide->setEndValue(target);
            slide->setEasingCurve(QEasingCurve::InOutQuad);
            slide->start(QAbstractAnimation::DeleteWhenStopped);
        } else {
            toast->move(target);
        }
        y += toast->height() + kGap;
    }
    raise();
}

} // namespace aluchop::gui

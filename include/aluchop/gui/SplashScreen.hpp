#pragma once

/**
 * @file SplashScreen.hpp
 * @brief Start-up splash screen and the in-window loading veil (SPEC §3 "Extra Features").
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Two distinct loading affordances live here:
 *  - SplashScreen  — the branded window shown while AppContext opens the database, migrates
 *                    the schema and seeds the menu, before the login window appears.
 *  - LoadingOverlay — a translucent veil laid over an already-visible screen while a slow
 *                    operation runs (report export, PDF render, backup/restore). Per
 *                    ARCHITECTURE §9 the application is single-threaded, so the veil is shown,
 *                    the event loop is pumped once to paint it, and the work then runs inline.
 */

#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QSplashScreen>
#include <QString>
#include <QVBoxLayout>

#include <functional>

namespace aluchop::gui {

/**
 * @brief Branded start-up splash: sage backdrop, wordmark, tagline and status line.
 *
 * Paints the AluChop logo and application name on the Sage-Green backdrop, fades in, holds,
 * then fades out and invokes the continuation supplied by main.cpp. Status text ("Opening
 * database…", "Seeding menu…") is pushed with the inherited QSplashScreen::showMessage().
 *
 * @par Ownership
 * Created on the stack (or on the heap without a parent) inside main.cpp and destroyed once
 * the login window is up; it never parents anything else.
 *
 * @oop-concept Single Inheritance :: specialises QSplashScreen with branded painting and a
 *              timed fade, reusing all of Qt's splash behaviour
 */
class SplashScreen : public QSplashScreen {
    Q_OBJECT
public:
    /// Builds the branded pixmap (logo + "AluChop" + tagline + credit line) and applies it.
    SplashScreen();

    /// Fades in, waits, fades out, then calls @p then.
    ///
    /// Implemented with QPropertyAnimation on the splash window opacity plus a single-shot
    /// QTimer; the callback runs on the GUI thread after the fade-out finishes, so main.cpp
    /// can construct the login window from inside it.
    ///
    /// @param ms   total time the splash stays fully visible, in milliseconds (1200 in main.cpp).
    /// @param then continuation invoked once the splash has closed. Must not be empty.
    ///
    /// @oop-concept Pass by Reference :: the callback is taken by const reference — no copy of
    ///              the caller's closure state
    void showFor(int ms, const std::function<void()>& then);
};

/**
 * @brief Translucent "please wait" veil pinned over its parent widget.
 *
 * Header-only and free of signals/slots on purpose: it needs no meta-object, so it costs the
 * build nothing and can be dropped into any screen. Derives from QFrame (not QWidget) so the
 * stylesheet background from ThemeManager actually paints — see the Qt gotcha documented in
 * ThemeManager.hpp.
 *
 * Typical use inside a page slot:
 * @code
 * m_loading->begin(tr("Exporting report…"));
 * const auto result = m_ctx.reports().exportCsv(kind, from, to);
 * m_loading->end();
 * @endcode
 *
 * @par Ownership
 * `new LoadingOverlay(this)` — parented, deleted by Qt with the page. Never wrapped in a
 * smart pointer (ARCHITECTURE §9 rule 1).
 */
class LoadingOverlay : public QFrame {
public:
    /// @param parent the widget to cover; the overlay tracks its size via an event filter.
    /// @param message initial caption; changed later with setMessage().
    ///
    /// @oop-concept Default Arguments :: the common case needs only the parent
    explicit LoadingOverlay(QWidget* parent, const QString& message = QStringLiteral("Loading…"))
        : QFrame(parent) {
        setObjectName(QStringLiteral("loadingOverlay"));
        auto* column = new QVBoxLayout(this);
        column->setAlignment(Qt::AlignCenter);
        column->setSpacing(14);

        m_label = new QLabel(message, this);
        m_label->setObjectName(QStringLiteral("loadingOverlayText"));
        m_label->setAlignment(Qt::AlignCenter);

        m_bar = new QProgressBar(this);
        m_bar->setObjectName(QStringLiteral("loadingOverlayBar"));
        m_bar->setRange(0, 0);        // busy indicator — duration is unknown
        m_bar->setTextVisible(false);
        m_bar->setFixedWidth(220);

        column->addWidget(m_label);
        column->addWidget(m_bar, 0, Qt::AlignHCenter);

        if (parent) {
            parent->installEventFilter(this);
            setGeometry(parent->rect());
        }
        hide();
    }

    /// Replaces the caption without showing or hiding the veil.
    void setMessage(const QString& message) { m_label->setText(message); }

    /// Shows the veil over the whole parent and pumps the event loop once so it is painted
    /// before the blocking call that follows.
    /// @param message caption to display.
    void begin(const QString& message) {
        setMessage(message);
        if (QWidget* p = parentWidget()) {
            setGeometry(p->rect());
        }
        raise();
        show();
        QCoreApplication::processEvents();
    }

    /// Hides the veil. Safe to call when it is already hidden.
    void end() { hide(); }

protected:
    /// Keeps the veil exactly the size of its parent.
    /// @return always defers to QFrame's handling — the filter only observes.
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Resize && watched == parentWidget()) {
            setGeometry(parentWidget()->rect());
        }
        return QFrame::eventFilter(watched, event);
    }

private:
    QLabel* m_label = nullptr;      ///< Qt-parent-owned caption.
    QProgressBar* m_bar = nullptr;  ///< Qt-parent-owned busy indicator.
};

} // namespace aluchop::gui

/**
 * @file SplashScreen.cpp
 * @brief Implementation of the branded start-up splash.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The splash is the first thing a marker sees, so it is composed rather than assembled: the whole
 * card is painted once into a single high-DPI pixmap — sage gradient, generated chef-hat mark,
 * wordmark, tagline, hairline rule and the SPEC §10 credit — while the two things that change at
 * run time, the status caption and the progress fill, are real child widgets laid over it so the
 * theme styles them like everything else and so neither can be clipped by the card edge.
 *
 * showFor() then runs the start-up choreography: fade in, sweep the progress fill across the
 * budget it was given while the database opens and seeds, fade out, and hand control to the
 * continuation main.cpp supplied.
 */

#include "aluchop/gui/SplashScreen.hpp"

#include "aluchop/core/AppInfo.hpp"
#include "aluchop/gui/ThemeManager.hpp"

#include <QAbstractAnimation>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QEasingCurve>
#include <QFont>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <QtSvg/QSvgRenderer>

namespace aluchop::gui {
namespace {

constexpr int kWidth = 560;    ///< Logical splash width in pixels.
constexpr int kHeight = 360;   ///< Logical splash height in pixels.
constexpr qreal kDpr = 2.0;    ///< Paint at 2x so the wordmark stays crisp on Retina panels.

constexpr int kBarWidth = 200;       ///< Progress-bar track width; centred under the caption.
constexpr int kBarHeight = 6;        ///< Must equal the min/max-height of `#splashProgress`.
constexpr int kProgressSteps = 1000; ///< Fine enough that the fill grows smoothly, not in jumps.
/// The fill the bar rests at before any choreography runs. It is not zero on purpose: a splash
/// that is merely shown — during the very first paint, or by the screenshot tool, which never
/// calls showFor() — must still read as a deliberate, round-capped, genuinely in-progress fill
/// rather than as an empty groove with a stray dot at one end.
constexpr int kProgressLead = 340;

/// Draws the generated chef-hat mark centred in @p box, in @p colour.
void paintMark(QPainter& painter, const QRectF& box, const QColor& colour) {
    const QString doc =
        QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
                       "stroke='%1' stroke-width='1.5' stroke-linecap='round' "
                       "stroke-linejoin='round'>"
                       "<path d='M7.4 20.6h9.2v-3.9H7.4z'/>"
                       "<path d='M7.4 16.7c-2.6-.5-4.3-2.6-4.3-5.1 0-2.9 2.3-5.2 5.2-5.2"
                       ".7-2 2.6-3.4 4.8-3.4s4.1 1.4 4.8 3.4c2.9 0 5.2 2.3 5.2 5.2 0 2.5"
                       "-1.7 4.6-4.3 5.1'/>"
                       "<path d='M10 16.7v-3.4M14 16.7v-3.4'/></svg>")
            .arg(colour.name(QColor::HexRgb));

    QSvgRenderer renderer(doc.toUtf8());
    if (renderer.isValid()) {
        renderer.render(&painter, box);
    }
}

/// Composes the full splash artwork.
QPixmap buildArtwork() {
    const Palette& p = ThemeManager::instance().palette();

    QPixmap canvas(static_cast<int>(kWidth * kDpr), static_cast<int>(kHeight * kDpr));
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.scale(kDpr, kDpr);   // paint in logical pixels; DPR is stamped on the pixmap below

    // --- backdrop: a deep sage gradient with rounded corners -----------------------------
    const QRectF card(0.5, 0.5, kWidth - 1.0, kHeight - 1.0);
    QLinearGradient gradient(card.topLeft(), card.bottomRight());
    gradient.setColorAt(0.0, p.primary.darker(112));
    gradient.setColorAt(0.55, p.primary);
    gradient.setColorAt(1.0, p.secondary.darker(105));

    QPainterPath rounded;
    rounded.addRoundedRect(card, 22, 22);
    painter.fillPath(rounded, gradient);

    // A single soft highlight arc keeps the flat fill from looking like plastic.
    painter.save();
    painter.setClipPath(rounded);
    QColor glow = p.accent;
    glow.setAlphaF(0.16f);
    painter.setBrush(glow);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QRectF(-160, -220, 520, 420));
    painter.restore();

    painter.setPen(QPen(QColor(255, 255, 255, 46), 1));
    painter.drawPath(rounded);

    // --- mark ----------------------------------------------------------------------------
    const QColor ink(255, 255, 255);
    paintMark(painter, QRectF((kWidth - 72) / 2.0, 58, 72, 72), ink);

    // --- wordmark and tagline -------------------------------------------------------------
    QFont title = QApplication::font();
    title.setPointSizeF(38.0);
    title.setWeight(QFont::Black);
    title.setLetterSpacing(QFont::AbsoluteSpacing, -0.5);
    painter.setFont(title);
    painter.setPen(ink);
    painter.drawText(QRect(0, 142, kWidth, 52), Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("AluChop"));

    QFont tagline = QApplication::font();
    tagline.setPointSizeF(11.5);
    tagline.setWeight(QFont::DemiBold);
    tagline.setLetterSpacing(QFont::AbsoluteSpacing, 2.6);
    painter.setFont(tagline);
    painter.setPen(QColor(255, 255, 255, 205));
    painter.drawText(QRect(0, 196, kWidth, 22), Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("RESTAURANT MANAGEMENT SYSTEM"));

    // --- hairline rule --------------------------------------------------------------------
    // Everything below this line is reserved for live widgets: the status caption, the busy bar
    // and the credit block. Nothing is painted into 240..296 so the caption can never collide
    // with artwork.
    painter.setPen(QPen(QColor(255, 255, 255, 60), 1));
    painter.drawLine(QPointF(kWidth / 2.0 - 110, 232), QPointF(kWidth / 2.0 + 110, 232));

    // --- SPEC §10 credit, quiet but present ------------------------------------------------
    QFont credit = QApplication::font();
    credit.setPointSizeF(10.0);
    credit.setWeight(QFont::Medium);
    painter.setFont(credit);
    painter.setPen(QColor(255, 255, 255, 170));
    painter.drawText(QRect(0, kHeight - 58, kWidth, 18), Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("Designed & Developed by %1")
                         .arg(QString::fromUtf8(core::kAppInfo.developer)));

    credit.setPointSizeF(9.0);
    painter.setFont(credit);
    painter.setPen(QColor(255, 255, 255, 130));
    painter.drawText(QRect(0, kHeight - 40, kWidth, 18), Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("%1  ·  %2")
                         .arg(QString::fromUtf8(core::kAppInfo.rollNo),
                              QString::fromUtf8(core::kAppInfo.email)));

    painter.end();
    canvas.setDevicePixelRatio(kDpr);
    return canvas;
}

} // namespace

/// @oop-concept Single Inheritance :: QSplashScreen already knows how to be a borderless,
/// always-on-top, screen-centred start-up window — this class only supplies the artwork,
/// a progress indicator and the fade choreography
SplashScreen::SplashScreen() : QSplashScreen(buildArtwork()) {
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowFlag(Qt::FramelessWindowHint, true);

    // The status caption is a real child widget sitting in the band reserved for it, because
    // QSplashScreen's own message painting puts the text 5 px from the very bottom edge — where
    // it was being sliced by the card's rounded corner and running into the credit block.
    auto* status = new QLabel(this);
    status->setObjectName(QStringLiteral("splashStatus"));
    status->setAlignment(Qt::AlignCenter);
    status->setGeometry(36, 244, kWidth - 72, 22);

    // The progress bar is a real child widget rather than painted artwork, so ThemeManager styles
    // it (via `#splashProgress`) exactly like every other progress bar in the application.
    //
    // It is deliberately *determinate*. Qt's busy indicator (range 0..0) wraps its moving chunk
    // around the ends of the groove, so at most moments it painted as two separate white slabs
    // with the track showing between them — the leading one pill-capped, the trailing one sliced
    // square where it ran off the track. A single fill that only ever grows has no wrap point, so
    // both of its ends stay identically capped at every width, and the sweep is honest: showFor()
    // is handed the start-up budget and animates the fill across exactly that.
    auto* bar = new QProgressBar(this);
    bar->setObjectName(QStringLiteral("splashProgress"));
    bar->setRange(0, kProgressSteps);
    bar->setValue(kProgressLead);
    bar->setTextVisible(false);
    bar->setGeometry((kWidth - kBarWidth) / 2, 278, kBarWidth, kBarHeight);

    // showMessage() is not virtual and the header contract adds no override, so the inherited
    // message is intercepted through the signal it already emits: the text is routed into the
    // caption above and the base class's copy is cleared so it is never painted twice. Clearing
    // re-emits the signal, which the echo flag swallows; an explicit clearMessage() from a caller
    // still empties the caption.
    connect(this, &QSplashScreen::messageChanged, this, [this, status](const QString& text) {
        if (property("aluchopMessageEcho").toBool()) {
            setProperty("aluchopMessageEcho", false);
            return;
        }
        status->setText(text);
        if (!text.isEmpty()) {
            setProperty("aluchopMessageEcho", true);
            QSplashScreen::clearMessage();
        }
    });
}

void SplashScreen::showFor(int ms, const std::function<void()>& then) {
    // The continuation is copied, not referenced: it has to outlive this call by the length of
    // the whole fade, long after the caller's expression has been destroyed.
    const std::function<void()> continuation = then;

    setWindowOpacity(0.0);
    show();
    QApplication::processEvents();   // paint the first frame before any blocking start-up work

    auto* fadeIn = new QPropertyAnimation(this, "windowOpacity", this);
    fadeIn->setDuration(320);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::InOutQuad);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);

    // The fill is animated across the same budget the caller gave the splash, so it reaches full
    // exactly as the fade-out starts rather than stopping somewhere arbitrary. OutCubic front-loads
    // the sweep the way real start-up work behaves: most of it lands early, the tail settles.
    //
    // QPropertyAnimation is driven from the unified timer's wall clock, so the long blocking spans
    // in main.cpp (opening, migrating and seeding the database) do not desynchronise it — the fill
    // simply resumes at the position the elapsed time says it should be at.
    if (auto* bar = findChild<QProgressBar*>(QStringLiteral("splashProgress"))) {
        auto* fill = new QPropertyAnimation(bar, "value", bar);
        fill->setDuration(ms > 0 ? ms : 1);
        fill->setStartValue(bar->value());   // continue from the resting fill, never snap back
        fill->setEndValue(kProgressSteps);
        fill->setEasingCurve(QEasingCurve::OutCubic);
        fill->start(QAbstractAnimation::DeleteWhenStopped);
    }

    QTimer::singleShot(ms > 0 ? ms : 0, this, [this, continuation]() {
        auto* fadeOut = new QPropertyAnimation(this, "windowOpacity", this);
        fadeOut->setDuration(320);
        fadeOut->setStartValue(windowOpacity());
        fadeOut->setEndValue(0.0);
        fadeOut->setEasingCurve(QEasingCurve::InOutQuad);
        connect(fadeOut, &QPropertyAnimation::finished, this, [this, continuation]() {
            close();
            if (continuation) {
                continuation();
            }
        });
        fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

} // namespace aluchop::gui

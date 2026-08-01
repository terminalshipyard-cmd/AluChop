/**
 * @file SplashScreen.cpp
 * @brief Implementation of the branded start-up splash.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The splash is the first thing a marker sees, so it is composed rather than assembled: the whole
 * card is painted once into a single high-DPI pixmap — sage gradient, generated chef-hat mark,
 * wordmark, tagline, hairline rule and the SPEC §10 credit — and a busy progress bar is laid over
 * it as a real child widget so the theme styles it like everything else.
 *
 * showFor() then runs the only animation the start-up path needs: fade in, hold while the
 * database opens and seeds, fade out, and hand control to the continuation main.cpp supplied.
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

    // The busy bar is a real child widget rather than painted artwork, so ThemeManager styles it
    // (via `#splashProgress`) exactly like every other progress bar in the application.
    auto* bar = new QProgressBar(this);
    bar->setObjectName(QStringLiteral("splashProgress"));
    bar->setRange(0, 0);            // indeterminate — start-up duration is genuinely unknown
    bar->setTextVisible(false);
    bar->setGeometry((kWidth - 220) / 2, 258, 220, 8);
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

/**
 * @file StatCard.cpp
 * @brief Implementation of the animated dashboard statistic tile.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * A StatCard is three labels and an icon inside a themed QFrame — the interest is entirely in
 * how it arrives on screen. setValue() counts the headline number up from zero instead of
 * snapping to it, and animateIn() fades the whole tile in while its content rises 12 px. Both
 * effects are driven by Qt's animation framework, both respect the "one QGraphicsEffect per
 * widget" rule, and neither one fights the layout: the rise is expressed as an animated top
 * content margin, so the layout stays authoritative about geometry at every frame.
 *
 * The tile never formats money itself — the dashboard passes a string already produced by
 * core::formatNpr, and the count-up simply re-renders the numeric part of it. It also knows when
 * to stay out of the way: a screen that animates the value itself (DashboardPage does, so it can
 * count in paisa and format through core::formatNpr on every tick) gets the raw string set
 * straight onto the label instead of a second, competing animation.
 */

#include "aluchop/gui/StatCard.hpp"

#include "aluchop/gui/ThemeManager.hpp"

#include <QColor>
#include <QDateTime>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QString>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QVariantAnimation>
#include <QtSvg/QSvgRenderer>

namespace aluchop::gui {
namespace {

/// @return stroke-based SVG body for @p key, or an empty string when the key is unknown.
///
/// The project ships no binary icon files, so every glyph in this screen is generated: the icon
/// path a caller hands to StatCard is treated as a *name* (its file stem), and the artwork is
/// drawn from these paths. If a real SVG file happens to exist at that path it wins.
QString glyphBody(const QString& key) {
    if (key == QLatin1String("sales") || key == QLatin1String("money"))
        return QStringLiteral("<rect x='2.5' y='6' width='19' height='12' rx='3'/>"
                              "<circle cx='12' cy='12' r='2.9'/>"
                              "<path d='M6 9.5v5M18 9.5v5'/>");
    if (key == QLatin1String("orders") || key == QLatin1String("pending"))
        return QStringLiteral("<circle cx='12' cy='12' r='8.8'/><path d='M12 6.6V12l3.6 2.2'/>");
    if (key == QLatin1String("customers") || key == QLatin1String("users"))
        return QStringLiteral("<circle cx='9.2' cy='8.4' r='3.5'/>"
                              "<path d='M3 20c0-3.3 2.8-5.4 6.2-5.4S15.4 16.7 15.4 20'/>"
                              "<path d='M16.2 5.4a3.4 3.4 0 0 1 0 6.4'/>"
                              "<path d='M17.6 15c2.1.7 3.4 2.5 3.4 5'/>");
    if (key == QLatin1String("inventory") || key == QLatin1String("stock"))
        return QStringLiteral("<path d='M12 2.9l8.4 4.4v9.4L12 21.1 3.6 16.7V7.3z'/>"
                              "<path d='M3.6 7.3L12 11.7l8.4-4.4'/><path d='M12 11.7v9.4'/>");
    if (key == QLatin1String("alert") || key == QLatin1String("lowstock"))
        return QStringLiteral("<path d='M12 3.6L21.4 20H2.6z'/><path d='M12 9.6v4.4'/>"
                              "<circle cx='12' cy='17.1' r='1.05'/>");
    if (key == QLatin1String("reservations") || key == QLatin1String("calendar"))
        return QStringLiteral("<rect x='3.4' y='5' width='17.2' height='16' rx='3'/>"
                              "<path d='M3.4 10h17.2M8 3v4M16 3v4'/>");
    // Neutral fallback so an unknown name still yields a considered mark, never a blank square.
    return QStringLiteral("<circle cx='12' cy='12' r='8.6'/><path d='M12 8.2v5.4'/>"
                          "<circle cx='12' cy='16.4' r='1'/>");
}

/// Renders @p iconPath at @p px logical pixels in @p colour.
/// @param iconPath a real SVG file path if one exists, otherwise a glyph name.
QPixmap renderGlyph(const QString& iconPath, const QColor& colour, int px) {
    const qreal dpr = 2.0; // crisp on Retina; harmless elsewhere
    QPixmap pm(static_cast<int>(px * dpr), static_cast<int>(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (!iconPath.isEmpty() && QFileInfo::exists(iconPath)) {
        QSvgRenderer file(iconPath);
        if (file.isValid()) {
            file.render(&painter, QRectF(0, 0, px, px));
            return pm;
        }
    }

    const QString key = QFileInfo(iconPath).completeBaseName().toLower();
    const QString doc =
        QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' "
                       "fill='none' stroke='%1' stroke-width='1.8' stroke-linecap='round' "
                       "stroke-linejoin='round'>%2</svg>")
            .arg(colour.name(QColor::HexRgb), glyphBody(key));

    QSvgRenderer renderer(doc.toUtf8());
    if (renderer.isValid()) {
        renderer.render(&painter, QRectF(0, 0, px, px));
    }
    return pm;
}

/// Splits a formatted figure into "Rs " / "1,250.00" / trailing text.
/// @return false when @p text carries no number to animate.
bool splitNumber(const QString& text, QString& prefix, double& value, int& decimals, QString& suffix) {
    static const QRegularExpression re(QStringLiteral("^(\\D*)([0-9][0-9,]*(?:\\.[0-9]+)?)(.*)$"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        return false;
    }
    prefix = m.captured(1);
    suffix = m.captured(3);

    QString digits = m.captured(2);
    const int dot = digits.indexOf(QLatin1Char('.'));
    decimals = (dot < 0) ? 0 : static_cast<int>(digits.size()) - dot - 1;
    digits.remove(QLatin1Char(','));

    bool ok = false;
    value = digits.toDouble(&ok);
    return ok;
}

/// Object names for this card's own animations, so they can be told apart from any animation a
/// screen installs on the same card.
const QString kCountUpName = QStringLiteral("statCardCountUp");
const QString kFadeInName = QStringLiteral("statCardFadeIn");
const QString kRiseName = QStringLiteral("statCardRise");

/// Milliseconds below which two consecutive setValue() calls must be a caller animating the card.
constexpr qint64 kDrivenWindowMs = 150;

/// @return true when something other than this card is already animating its headline value.
///
/// Two independent signals are consulted, because either alone is fooled: a foreign animation
/// that is currently Running proves a driver exists during the ticks, and a setValue() that
/// arrives within a frame of the previous one catches the driver's final "snap to the exact
/// figure" call, which happens after its animation has already stopped.
bool isDrivenFromOutside(StatCard* card) {
    for (QVariantAnimation* anim : card->findChildren<QVariantAnimation*>()) {
        const QString name = anim->objectName();
        if (name == kCountUpName || name == kFadeInName || name == kRiseName) {
            continue;   // one of ours
        }
        if (anim->state() == QAbstractAnimation::Running) {
            return true;
        }
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 previous = card->property("lastValueSetMs").toLongLong();
    card->setProperty("lastValueSetMs", now);
    return previous != 0 && (now - previous) < kDrivenWindowMs;
}

} // namespace

/// @oop-concept Parameterised Constructor :: a card without a caption and a mark is meaningless,
/// so there is no default constructor to misuse
StatCard::StatCard(const QString& title, const QString& iconSvgPath, QWidget* parent)
    : QFrame(parent) {
    setObjectName(QStringLiteral("statCard"));
    setFrameShape(QFrame::NoFrame);
    setMinimumHeight(126);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(18, 16, 18, 16);
    column->setSpacing(4);

    // --- header row: caption on the left, tinted glyph on the right ---------------------
    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);

    m_title = new QLabel(title.toUpper(), this);
    m_title->setObjectName(QStringLiteral("statCardTitle"));

    m_icon = new QLabel(this);
    m_icon->setObjectName(QStringLiteral("statCardIcon"));
    m_icon->setFixedSize(36, 36);
    m_icon->setAlignment(Qt::AlignCenter);
    m_icon->setProperty("glyphPath", iconSvgPath);
    m_icon->setPixmap(renderGlyph(iconSvgPath, ThemeManager::instance().palette().primary, 20));

    header->addWidget(m_title, 0, Qt::AlignVCenter);
    header->addStretch(1);
    header->addWidget(m_icon, 0, Qt::AlignVCenter);

    // --- headline number ----------------------------------------------------------------
    m_value = new QLabel(QStringLiteral("—"), this);
    m_value->setObjectName(QStringLiteral("statCardValue"));

    // --- trend line, hidden until setDelta() gives it something to say -------------------
    m_delta = new QLabel(this);
    m_delta->setObjectName(QStringLiteral("statCardDelta"));
    m_delta->setVisible(false);

    column->addLayout(header);
    column->addSpacing(2);
    column->addWidget(m_value);
    column->addWidget(m_delta);
    column->addStretch(1);

    // The glyph is a raster pixmap, so unlike everything driven by the stylesheet it has to be
    // re-rendered when the palette changes.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        const QString path = m_icon->property("glyphPath").toString();
        m_icon->setPixmap(renderGlyph(path, ThemeManager::instance().palette().primary, 20));
    });
}

void StatCard::setValue(const QString& value) {
    QString prefix, suffix;
    double target = 0.0;
    int decimals = 0;

    // A previous count-up must be abandoned before a new one starts, or the two animations fight
    // over the same label. The running animation is found as a named child rather than stored in
    // a member, which keeps the public header exactly as the contract froze it.
    if (auto* running = findChild<QVariantAnimation*>(kCountUpName)) {
        running->setObjectName(QString());   // so it can never be found twice
        running->stop();
        running->deleteLater();
    }

    // The card's own count-up is a courtesy, not a mandate: the frozen header says the *caller*
    // supplies an already-formatted value, and DashboardPage exercises that right by running its
    // own animation over paisa and pushing a formatted string on every tick. Two count-ups on one
    // label would visibly fight, so the card stands down whenever it can tell it is being driven
    // from outside — either because a foreign animation is live on this card right now, or
    // because the previous setValue() arrived only a frame ago.
    if (isDrivenFromOutside(this)) {
        m_value->setText(value);
        return;
    }

    if (!splitNumber(value, prefix, target, decimals, suffix) || target <= 0.0) {
        m_value->setText(value);
        return;
    }

    const QLocale grouping(QLocale::English);
    auto* anim = new QVariantAnimation(this);
    anim->setObjectName(kCountUpName);
    anim->setStartValue(0.0);
    anim->setEndValue(target);
    anim->setDuration(750);
    anim->setEasingCurve(QEasingCurve::OutCubic);

    connect(anim, &QVariantAnimation::valueChanged, this,
            [this, prefix, suffix, decimals, grouping](const QVariant& v) {
                m_value->setText(prefix + grouping.toString(v.toDouble(), 'f', decimals) + suffix);
            });
    // Snap to the caller's exact string at the end so the displayed figure is never a rounding
    // artefact of the animation.
    connect(anim, &QVariantAnimation::finished, this,
            [this, value]() { m_value->setText(value); });

    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void StatCard::setDelta(const QString& delta, bool positive) {
    m_delta->setText(delta);
    m_delta->setVisible(!delta.isEmpty());

    // The colour is a stylesheet decision, not a widget decision: the tile only states the
    // direction and lets ThemeManager's `#statCardDelta[trend="up"]` rule pick the green.
    m_delta->setProperty("trend", positive ? QStringLiteral("up") : QStringLiteral("down"));
    m_delta->style()->unpolish(m_delta);
    m_delta->style()->polish(m_delta);
}

/// @oop-concept Default Arguments :: a lone card animates immediately; the dashboard staggers
/// its four tiles by passing 0 / 80 / 160 / 240 ms
void StatCard::animateIn(int delayMs) {
    auto* fade = new QGraphicsOpacityEffect(this);
    fade->setOpacity(0.0);
    setGraphicsEffect(fade); // Qt owns it and deletes any previous effect

    // Parented to the card's *container*, not to the card. A screen is entitled to stop every
    // animation it finds on a tile before starting its own value animation (DashboardPage does
    // exactly that), and QPropertyAnimation IS-A QVariantAnimation, so an entrance animation
    // parented to the card would be swept away by that perfectly reasonable housekeeping. The
    // animation's target stays the card's own effect, and Qt guards a destroyed target, so the
    // lifetime is still safe either way.
    QObject* animationHost = parentWidget() ? static_cast<QObject*>(parentWidget())
                                            : static_cast<QObject*>(this);

    auto* opacity = new QPropertyAnimation(fade, "opacity", animationHost);
    opacity->setObjectName(kFadeInName);
    opacity->setDuration(320);
    opacity->setStartValue(0.0);
    opacity->setEndValue(1.0);
    opacity->setEasingCurve(QEasingCurve::InOutQuad);

    // Safety net. QPropertyAnimation IS-A QVariantAnimation, and a screen that stops every
    // animation on a card (DashboardPage does exactly that before it starts its own count-up)
    // could otherwise leave this tile frozen half-faded. Whenever the animation stops — finished
    // or cancelled — the card snaps to its settled appearance.
    connect(opacity, &QPropertyAnimation::stateChanged, this,
            [fade](QAbstractAnimation::State state, QAbstractAnimation::State) {
                if (state == QAbstractAnimation::Stopped) {
                    fade->setOpacity(1.0);
                }
            });

    // The 12 px "rise" is applied as a shrinking top margin rather than by moving the widget:
    // moving a layout-managed widget is undone by the next layout pass, whereas a margin IS the
    // layout, so the motion is stable no matter what else resizes.
    auto* rise = new QVariantAnimation(animationHost);
    rise->setObjectName(kRiseName);
    rise->setStartValue(12);
    rise->setEndValue(0);
    rise->setDuration(320);
    rise->setEasingCurve(QEasingCurve::OutCubic);
    connect(rise, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const int lift = v.toInt();
        if (QLayout* box = layout()) {
            box->setContentsMargins(18, 16 + lift, 18, 16 - lift);
        }
    });
    connect(rise, &QVariantAnimation::stateChanged, this,
            [this](QAbstractAnimation::State state, QAbstractAnimation::State) {
                if (state == QAbstractAnimation::Stopped) {
                    if (QLayout* box = layout()) {
                        box->setContentsMargins(18, 16, 18, 16);   // settle, however it ended
                    }
                }
            });

    const auto launch = [opacity, rise]() {
        opacity->start(QAbstractAnimation::DeleteWhenStopped);
        rise->start(QAbstractAnimation::DeleteWhenStopped);
    };

    if (delayMs > 0) {
        QTimer::singleShot(delayMs, this, launch);
    } else {
        launch();
    }
}

} // namespace aluchop::gui

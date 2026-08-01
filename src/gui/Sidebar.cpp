/**
 * @file Sidebar.cpp
 * @brief Implementation of the left navigation rail.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The rail is deliberately the dumbest widget in the application: it knows how many entries it
 * has, which one is lit, and nothing else. Picking an entry emits navigate(index) and the shell
 * decides what that means.
 *
 * Two presentation details are worth calling out:
 *
 * 1. **The icons are drawn, not loaded.** The project ships no binary icon assets, so an entry's
 *    "icon path" is treated as a *name*: the rail generates stroke-based SVG artwork for it in
 *    the palette's colours, in two states (muted when idle, brand green when active). If a real
 *    SVG file does exist at the given path it is used instead, so adding artwork later needs no
 *    code change.
 *
 * 2. **The selection indicator is a real widget, not a repaint.** A 4 px pill slides between
 *    entries with a QPropertyAnimation, which is what makes navigation feel continuous rather
 *    than like nine independent buttons switching on and off.
 */

#include "aluchop/gui/Sidebar.hpp"

#include "aluchop/gui/ThemeManager.hpp"

#include <QAbstractAnimation>
#include <QColor>
#include <QEasingCurve>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QObject>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRect>
#include <QSize>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtSvg/QSvgRenderer>

#include <functional>
#include <utility>

namespace aluchop::gui {
namespace {

constexpr int kRailWidth = 232;      ///< Wide enough for icon + label, narrow enough to ignore.
constexpr int kButtonHeight = 44;    ///< Comfortable touch/pointer target; nothing cramped.
constexpr int kIndicatorWidth = 4;   ///< The sliding selection pill.

/// Event forwarder — Sidebar's frozen header declares no `resizeEvent`, so geometry-dependent
/// work is driven through an installed filter instead of an override.
class EventHook : public QObject {
public:
    EventHook(QObject* owner, std::function<void(QEvent*)> onEvent)
        : QObject(owner), m_onEvent(std::move(onEvent)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        m_onEvent(event);
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void(QEvent*)> m_onEvent;
};

/// @return stroke-based SVG body for a navigation glyph name.
QString navGlyphBody(const QString& key) {
    if (key == QLatin1String("dashboard"))
        return QStringLiteral("<rect x='3.2' y='3.2' width='7.6' height='7.6' rx='2.2'/>"
                              "<rect x='13.2' y='3.2' width='7.6' height='7.6' rx='2.2'/>"
                              "<rect x='3.2' y='13.2' width='7.6' height='7.6' rx='2.2'/>"
                              "<rect x='13.2' y='13.2' width='7.6' height='7.6' rx='2.2'/>");
    if (key == QLatin1String("menu"))
        return QStringLiteral("<rect x='4' y='2.6' width='16' height='18.8' rx='3'/>"
                              "<path d='M8 8h8M8 12h8M8 16h5'/>");
    if (key == QLatin1String("orders"))
        return QStringLiteral("<rect x='5' y='4' width='14' height='17.2' rx='2.6'/>"
                              "<rect x='9' y='2.2' width='6' height='4' rx='1.6'/>"
                              "<path d='M9 11.4h6M9 15.4h6'/>");
    if (key == QLatin1String("customers"))
        return QStringLiteral("<circle cx='9.2' cy='8.4' r='3.5'/>"
                              "<path d='M3 20c0-3.3 2.8-5.4 6.2-5.4S15.4 16.7 15.4 20'/>"
                              "<path d='M16.2 5.4a3.4 3.4 0 0 1 0 6.4'/>"
                              "<path d='M17.6 15c2.1.7 3.4 2.5 3.4 5'/>");
    if (key == QLatin1String("employees"))
        return QStringLiteral("<rect x='3.2' y='5.2' width='17.6' height='15.4' rx='3'/>"
                              "<circle cx='9.4' cy='11' r='2.4'/>"
                              "<path d='M5.9 17.2c0-1.9 1.6-3.1 3.5-3.1s3.5 1.2 3.5 3.1'/>"
                              "<path d='M15.6 10.4h3.6M15.6 14h3.6'/><path d='M9 5.2V3.4'/>");
    if (key == QLatin1String("inventory"))
        return QStringLiteral("<path d='M12 2.9l8.4 4.4v9.4L12 21.1 3.6 16.7V7.3z'/>"
                              "<path d='M3.6 7.3L12 11.7l8.4-4.4'/><path d='M12 11.7v9.4'/>");
    if (key == QLatin1String("reservations"))
        return QStringLiteral("<rect x='3.4' y='5' width='17.2' height='16' rx='3'/>"
                              "<path d='M3.4 10h17.2M8 3v4M16 3v4'/>"
                              "<path d='M8 14h2.4M13.6 14H16M8 17.4h2.4'/>");
    if (key == QLatin1String("reports"))
        return QStringLiteral("<path d='M4 20.2h16'/>"
                              "<rect x='6' y='11.2' width='3.4' height='6.4' rx='1.3'/>"
                              "<rect x='11.3' y='6.6' width='3.4' height='11' rx='1.3'/>"
                              "<rect x='16.6' y='14' width='3.4' height='3.6' rx='1.3'/>");
    if (key == QLatin1String("settings"))
        return QStringLiteral("<circle cx='12' cy='12' r='3.2'/>"
                              "<path d='M12 2.7v3M12 18.3v3M21.3 12h-3M5.7 12h-3'/>"
                              "<path d='M18.6 5.4l-2.1 2.1M7.5 16.5l-2.1 2.1M18.6 18.6l-2.1-2.1"
                              "M7.5 7.5L5.4 5.4'/>");
    if (key == QLatin1String("brand"))
        return QStringLiteral("<path d='M7.4 20.6h9.2v-3.9H7.4z'/>"
                              "<path d='M7.4 16.7c-2.6-.5-4.3-2.6-4.3-5.1 0-2.9 2.3-5.2 5.2-5.2"
                              ".7-2 2.6-3.4 4.8-3.4s4.1 1.4 4.8 3.4c2.9 0 5.2 2.3 5.2 5.2 0 2.5"
                              "-1.7 4.6-4.3 5.1'/>");
    return QStringLiteral("<circle cx='12' cy='12' r='8.6'/>");
}

/// Renders an entry glyph in @p colour, preferring a real SVG file when one exists at @p path.
QPixmap renderNavGlyph(const QString& path, const QColor& colour, int px) {
    const qreal dpr = 2.0;
    QPixmap pm(static_cast<int>(px * dpr), static_cast<int>(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (!path.isEmpty() && QFileInfo::exists(path)) {
        QSvgRenderer file(path);
        if (file.isValid()) {
            file.render(&painter, QRectF(0, 0, px, px));
            return pm;
        }
    }

    const QString key = QFileInfo(path).completeBaseName().toLower();
    const QString doc =
        QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
                       "stroke='%1' stroke-width='1.7' stroke-linecap='round' "
                       "stroke-linejoin='round'>%2</svg>")
            .arg(colour.name(QColor::HexRgb), navGlyphBody(key));

    QSvgRenderer renderer(doc.toUtf8());
    if (renderer.isValid()) {
        renderer.render(&painter, QRectF(0, 0, px, px));
    }
    return pm;
}

/// Two-state icon: muted while idle, brand green while checked.
QIcon navIcon(const QString& path, const Palette& p) {
    QIcon icon;
    icon.addPixmap(renderNavGlyph(path, p.textMuted, 20), QIcon::Normal, QIcon::Off);
    icon.addPixmap(renderNavGlyph(path, p.text, 20), QIcon::Active, QIcon::Off);
    icon.addPixmap(renderNavGlyph(path, p.primary, 20), QIcon::Normal, QIcon::On);
    icon.addPixmap(renderNavGlyph(path, p.primary, 20), QIcon::Active, QIcon::On);
    return icon;
}

/// @return the rail's backdrop — a whisper of accent over the card colour, matching the `@rail@`
///         token ThemeManager bakes into the sheet.
///
/// A plain QWidget does not paint a style-sheet background (see the gotcha in ThemeManager.hpp),
/// so the rail must colour itself through its QPalette; this keeps the two routes in step.
QColor railColour(const Palette& p, ThemeManager::Mode mode) {
    const double ratio = (mode == ThemeManager::Mode::Light) ? 0.14 : 0.05;
    const auto lerp = [ratio](int a, int b) {
        return static_cast<int>(a + (b - a) * ratio + 0.5);
    };
    return QColor(lerp(p.card.red(), p.accent.red()),
                  lerp(p.card.green(), p.accent.green()),
                  lerp(p.card.blue(), p.accent.blue()));
}

} // namespace

Sidebar::Sidebar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("sidebar"));
    setFixedWidth(kRailWidth);
    setAutoFillBackground(true);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(14, 22, 14, 18);
    column->setSpacing(0);

    // --- wordmark -----------------------------------------------------------------------
    auto* brandRow = new QHBoxLayout();
    brandRow->setContentsMargins(8, 0, 0, 0);
    brandRow->setSpacing(11);

    auto* mark = new QLabel(this);
    mark->setObjectName(QStringLiteral("brandMark"));
    mark->setFixedSize(30, 30);
    mark->setAlignment(Qt::AlignCenter);

    auto* words = new QVBoxLayout();
    words->setContentsMargins(0, 0, 0, 0);
    words->setSpacing(0);

    auto* brand = new QLabel(QStringLiteral("AluChop"), this);
    brand->setObjectName(QStringLiteral("brandLabel"));
    auto* tagline = new QLabel(QStringLiteral("RESTAURANT MANAGER"), this);
    tagline->setObjectName(QStringLiteral("brandTagline"));

    words->addWidget(brand);
    words->addWidget(tagline);

    brandRow->addWidget(mark, 0, Qt::AlignVCenter);
    brandRow->addLayout(words);
    brandRow->addStretch(1);

    column->addLayout(brandRow);
    column->addSpacing(26);

    // --- navigation entries live in their own container so the sliding indicator can be
    //     positioned in the container's coordinate space ----------------------------------
    auto* nav = new QWidget(this);
    nav->setObjectName(QStringLiteral("sidebarNav"));
    auto* navColumn = new QVBoxLayout(nav);
    navColumn->setContentsMargins(0, 0, 0, 0);
    navColumn->setSpacing(3);

    auto* indicator = new QFrame(nav);
    indicator->setObjectName(QStringLiteral("sidebarIndicator"));
    indicator->setFrameShape(QFrame::NoFrame);
    indicator->setFixedWidth(kIndicatorWidth);
    indicator->resize(kIndicatorWidth, 22);
    indicator->hide();
    indicator->raise();

    column->addWidget(nav);
    column->addStretch(1);

    // --- footer hint --------------------------------------------------------------------
    auto* hint = new QLabel(QStringLiteral("Press  Ctrl+K  to search everywhere"), this);
    hint->setObjectName(QStringLiteral("mutedLabel"));
    hint->setWordWrap(true);
    hint->setContentsMargins(8, 0, 8, 0);
    column->addWidget(hint);

    // --- palette-driven surfaces ---------------------------------------------------------
    const auto repaintSurfaces = [this, mark]() {
        ThemeManager& theme = ThemeManager::instance();
        const Palette& p = theme.palette();

        QPalette pal = palette();
        pal.setColor(QPalette::Window, railColour(p, theme.mode()));
        setPalette(pal);

        mark->setPixmap(renderNavGlyph(QStringLiteral("brand"), p.primary, 26));

        for (QToolButton* button : m_buttons) {
            button->setIcon(navIcon(button->property("glyphPath").toString(), p));
        }
    };
    repaintSurfaces();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, repaintSurfaces);

    // Geometry-dependent placement of the indicator, without a resizeEvent override.
    installEventFilter(new EventHook(this, [this](QEvent* event) {
        const QEvent::Type t = event->type();
        if (t == QEvent::Resize || t == QEvent::Show) {
            setActive(m_active);
        }
    }));
}

/// @oop-concept STL vector :: entries are appended at runtime, so the button list is a
/// std::vector of raw, Qt-parent-owned pointers — a smart pointer here would double-delete
void Sidebar::addEntry(const QString& iconSvgPath, const QString& label) {
    QWidget* nav = findChild<QWidget*>(QStringLiteral("sidebarNav"));
    if (!nav || !nav->layout()) {
        return;
    }

    const int index = static_cast<int>(m_buttons.size());

    auto* button = new QToolButton(nav);
    button->setObjectName(QStringLiteral("sidebarButton"));
    button->setText(QStringLiteral("  ") + label);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setIconSize(QSize(20, 20));
    button->setMinimumHeight(kButtonHeight);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setToolTip(label);
    button->setProperty("glyphPath", iconSvgPath);
    button->setIcon(navIcon(iconSvgPath, ThemeManager::instance().palette()));

    // The index is captured by value: an entry's meaning is fixed the moment it is added.
    connect(button, &QToolButton::clicked, this, [this, index]() {
        setActive(index);
        emit navigate(index);
    });

    qobject_cast<QVBoxLayout*>(nav->layout())->addWidget(button);
    m_buttons.push_back(button);

    if (index == m_active) {
        // Geometry is not valid until the layout has run, so light the first entry once the
        // event loop has caught up.
        QTimer::singleShot(0, this, [this]() { setActive(m_active); });
    }
}

void Sidebar::setActive(int index) {
    if (m_buttons.empty()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(m_buttons.size())) {
        return;   // out-of-range is a no-op so callers need no bounds test
    }

    m_active = index;
    for (std::size_t i = 0; i < m_buttons.size(); ++i) {
        m_buttons[i]->setChecked(static_cast<int>(i) == index);
    }

    auto* indicator = findChild<QFrame*>(QStringLiteral("sidebarIndicator"));
    QToolButton* active = m_buttons[static_cast<std::size_t>(index)];
    if (!indicator || active->height() <= 0) {
        return;
    }

    const int pillHeight = active->height() - 16;
    const QRect target(0, active->y() + 8, kIndicatorWidth, pillHeight);

    if (!indicator->isVisible()) {
        indicator->setGeometry(target);
        indicator->show();
        indicator->raise();
        return;
    }
    if (indicator->geometry() == target) {
        return;
    }

    auto* slide = new QPropertyAnimation(indicator, "geometry", indicator);
    slide->setDuration(200);
    slide->setStartValue(indicator->geometry());
    slide->setEndValue(target);
    slide->setEasingCurve(QEasingCurve::InOutQuad);
    slide->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace aluchop::gui

/**
 * @file ThemeManager.cpp
 * @brief Implementation of the Sage-Green design system and its runtime QSS generator.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Everything the application looks like is decided in this one translation unit. A ::Palette is
 * pure data; styleSheet() turns it into a complete application-wide Qt Style Sheet, and apply()
 * pushes that sheet (plus a matching QPalette) into the running QApplication. Switching mode
 * therefore restyles every open window live, with no restart and no widget knowing a colour.
 *
 * @section contrast Why the sheet computes its own foreground colours
 * A palette entry says what a surface *is*; it does not say what is legible *on* it. Pairing a
 * fill with a hard-coded foreground is exactly how the dark theme ended up with near-white text
 * on pale sage buttons (3.0:1 — unreadable). Every filled control here therefore takes its text
 * colour from onColour(), which measures WCAG relative luminance and returns whichever of the two
 * house inks actually contrasts. Both themes are held at or above the WCAG AA 4.5:1 body-text
 * threshold by construction rather than by hope.
 *
 * @section glyphs Why this file writes tiny SVG files
 * Qt Style Sheets can only reference sub-control artwork (check marks, combo-box chevrons,
 * spin-box arrows) through @c image:url(...). Qt resolves that URL against the filesystem or a
 * Qt resource — it does not accept inline @c data: URIs. The project deliberately ships no
 * binary icon assets, so the sheet's artwork is *generated*: a handful of ~200-byte SVG files
 * are written into a per-theme folder under QDir::tempPath() whenever a sheet is built, tinted
 * with the active palette. If the write fails the sheet still applies; the affected sub-control
 * simply falls back to its plain tinted box, so this can never break the UI.
 */

#include "aluchop/gui/ThemeManager.hpp"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QIcon>
#include <QIODevice>
#include <QLatin1String>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QProxyStyle>
#include <QStringList>
#include <QStyleFactory>
#include <QVector>
#include <cmath>
#include <utility>

namespace aluchop::gui {

// ---------------------------------------------------------------------------------------------
// The two immutable palettes.
//
// kLight is SPEC §1 verbatim; kDark is the derived deep desaturated green-grey counterpart
// mandated by ARCHITECTURE §3.5. The last three entries of each are the derived working colours
// (muted text, hover wash, drop-shadow colour) that the generated sheet leans on constantly.
// ---------------------------------------------------------------------------------------------

/// @oop-concept Constant Objects :: the light theme is a compile-time constant, not configuration
const Palette ThemeManager::kLight{
    QColor(93, 122, 102),    // primary     #5D7A66
    QColor(126, 155, 132),   // secondary   #7E9B84
    QColor(168, 195, 161),   // accent      #A8C3A1
    QColor(246, 248, 244),   // background  #F6F8F4
    QColor(255, 255, 255),   // card        #FFFFFF
    QColor(215, 228, 210),   // border      #D7E4D2
    QColor(94, 158, 102),    // success     #5E9E66
    QColor(209, 100, 100),   // danger      #D16464
    QColor(31, 45, 31),      // text        #1F2D1F
    QColor(99, 115, 99),     // textMuted   #637363  (derived — 5.0:1 on card, 4.7:1 on backdrop)
    QColor(237, 242, 233),   // hover       #EDF2E9  (derived)
    QColor(31, 45, 31, 77)   // shadow      text @ 30 %
};

/// @oop-concept Constant Objects :: the dark theme keeps the same hues, never pure black
///
/// The surfaces are deep desaturated green-greys with a deliberate one-step tonal ladder —
/// backdrop #161E17 → card #263127 → hairline #394636 — because a dark card can only read as
/// *raised* if it is measurably lighter than what it sits on (1.26:1 here) and is outlined by a
/// visible hairline (1.45:1). A drop shadow contributes nothing over a dark backdrop, so
/// elevation is carried by tone, exactly as it is in every serious dark UI.
const Palette ThemeManager::kDark{
    QColor(126, 155, 132),   // primary     #7E9B84
    QColor(93, 122, 102),    // secondary   #5D7A66
    QColor(168, 195, 161),   // accent      #A8C3A1
    QColor(22, 30, 23),      // background  #161E17
    QColor(38, 49, 39),      // card        #263127
    QColor(57, 70, 54),      // border      #394636
    QColor(111, 191, 119),   // success     #6FBF77
    QColor(224, 122, 122),   // danger      #E07A7A
    QColor(231, 239, 229),   // text        #E7EFE5
    QColor(163, 181, 161),   // textMuted   #A3B5A1  (derived — 6.4:1 on card)
    QColor(47, 59, 48),      // hover       #2F3B30  (derived)
    QColor(0, 0, 0, 115)     // shadow      black @ 45 %
};

namespace {

// ---------------------------------------------------------------------------------------------
// Small colour helpers used only while assembling the sheet.
// ---------------------------------------------------------------------------------------------

/// @return @p c as "#rrggbb" — the form QSS understands everywhere.
/// @oop-concept Inline Functions :: a one-expression helper called dozens of times per sheet
inline QString hex(const QColor& c) {
    return c.name(QColor::HexRgb);
}

/// @return @p c as a QSS "rgba(r,g,b,a)" literal with @p alpha in 0..1.
QString rgba(const QColor& c, double alpha) {
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(QString::number(alpha, 'f', 3));
}

/// @return @p base blended @p ratio of the way towards @p other (0 = base, 1 = other).
///
/// Used for the derived surfaces the palette does not name explicitly — the header tint, the
/// table zebra stripe, the scroll-bar handle — so those stay in tune in both themes.
QColor mix(const QColor& base, const QColor& other, double ratio) {
    const auto lerp = [ratio](int a, int b) {
        return static_cast<int>(a + (b - a) * ratio + 0.5);
    };
    return QColor(lerp(base.red(), other.red()),
                  lerp(base.green(), other.green()),
                  lerp(base.blue(), other.blue()));
}

/// @return the WCAG 2.1 relative luminance of @p c (0 = black, 1 = white).
double relativeLuminance(const QColor& c) {
    const auto channel = [](int raw) {
        const double v = raw / 255.0;
        return (v <= 0.04045) ? (v / 12.92) : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) + 0.0722 * channel(c.blue());
}

/// @return the WCAG contrast ratio between @p a and @p b, from 1.0 (identical) to 21.0.
double contrastRatio(const QColor& a, const QColor& b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return ((la > lb ? la : lb) + 0.05) / ((la > lb ? lb : la) + 0.05);
}

/// The two house inks. Every filled control's label is one of these — never a third colour, so
/// buttons stay a family rather than a collection.
const QColor kInk(17, 24, 17);        ///< #111811 — deepest green-black, for pale fills.
const QColor kPaper(255, 255, 255);   ///< #FFFFFF — for deep fills.

/// @return whichever house ink is genuinely legible on @p fill.
///
/// This is the whole fix for the dark theme's illegible call-to-action buttons: pale sage
/// (#7E9B84) scores 3.0:1 against white and 5.9:1 against ink, so the sheet now paints ink on it
/// automatically instead of trusting a hand-written pairing.
QColor onColour(const QColor& fill) {
    return contrastRatio(kInk, fill) >= contrastRatio(kPaper, fill) ? kInk : kPaper;
}

/**
 * @brief Builds a QSS font-family list containing only families that actually exist here.
 *
 * The sheet used to lead with "SF Pro Text", which is not an enumerable family on macOS: Qt
 * failed to match it, walked the whole font database looking for an alias, and logged
 * "Populating font family aliases took N ms. Replace uses of missing font family …" on every
 * single launch. The same trap catches the CSS generic names — Qt's CSS parser has no notion of
 * `sans-serif` or `monospace` and passes them through as literal family names — so neither the
 * fantasy family nor the generic tail may appear in the output.
 *
 * @param platform the platform's own font for this role, taken from QFontDatabase.
 * @param candidates portable fall-backs, tried in order and kept only when installed.
 * @return a ready-to-substitute QSS value such as `".AppleSystemUIFont", "Helvetica Neue"`.
 */
QString fontStack(const QFont& platform, std::initializer_list<const char*> candidates) {
    QStringList families;
    if (!platform.family().isEmpty()) {
        families << platform.family();
    }
    for (const char* candidate : candidates) {
        const QString family = QString::fromLatin1(candidate);
        if (!families.contains(family) && QFontDatabase::hasFamily(family)) {
            families << family;
        }
    }
    if (families.isEmpty()) {
        families << QStringLiteral("Helvetica");   // present on every Qt-supported desktop
    }

    QStringList quoted;
    for (const QString& family : std::as_const(families)) {
        quoted << QStringLiteral("\"%1\"").arg(family);
    }
    return quoted.join(QStringLiteral(", "));
}

/// @return the interface font stack, led by the platform's own UI font.
QString uiFontStack() {
    return fontStack(QFontDatabase::systemFont(QFontDatabase::GeneralFont),
                     {"Helvetica Neue", "Segoe UI", "Inter", "Noto Sans", "DejaVu Sans"});
}

/// @return the fixed-pitch stack used by the receipt preview, where column alignment is the
///         entire point and a proportional fall-back would misalign every price.
QString monoFontStack() {
    return fontStack(QFontDatabase::systemFont(QFontDatabase::FixedFont),
                     {"Menlo", "Consolas", "DejaVu Sans Mono", "Courier New"});
}

// ---------------------------------------------------------------------------------------------
// Generated sub-control artwork (see the file-level note).
// ---------------------------------------------------------------------------------------------

/**
 * @brief The application style: Fusion, plus the one standard icon that must not look native.
 *
 * The clear button inside every searchable QLineEdit is not a style-sheet sub-control — Qt builds
 * it from @c QStyle::SP_LineEditClearButton, which on this platform is a filled black disc with a
 * white cross in it. Dropped onto a sage card it reads as a foreign macOS control.
 *
 * It cannot be re-skinned from the sheet: the only selector that reaches it,
 * `QLineEdit > QToolButton`, also matches the icon buttons that QLineEdit::addAction() creates —
 * so a `qproperty-icon` there would overwrite LoginWindow's password-reveal eye as well
 * (verified). Overriding the icon at its source hits the clear button and nothing else.
 *
 * @oop-concept Method Overriding + Runtime Polymorphism :: one virtual is overridden; every other
 *              style query is forwarded to Fusion by the QProxyStyle base
 */
class AluChopStyle : public QProxyStyle {
public:
    /// @param base the concrete style to decorate; QProxyStyle takes ownership.
    explicit AluChopStyle(QStyle* base) : QProxyStyle(base) {}

    /// @copydoc QProxyStyle::standardIcon
    QIcon standardIcon(StandardPixmap which, const QStyleOption* option,
                       const QWidget* widget) const override {
        if (which == SP_LineEditClearButton) {
            return clearButtonIcon();
        }
        return QProxyStyle::standardIcon(which, option, widget);
    }

private:
    /// @return a hairline cross in the one sage that both palettes share (kLight::secondary is
    ///         kDark::primary, #7E9B84), so a single icon is legible on the white card (3.0:1)
    ///         and on the dark card (5.9:1) and can never go stale across a live theme switch.
    static QIcon clearButtonIcon() {
        constexpr int kSide = 16;
        constexpr qreal kDpr = 2.0;
        QPixmap pm(static_cast<int>(kSide * kDpr), static_cast<int>(kSide * kDpr));
        pm.setDevicePixelRatio(kDpr);
        pm.fill(Qt::transparent);

        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(ThemeManager::kLight.secondary);
        pen.setWidthF(1.9);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(QPointF(4.6, 4.6), QPointF(11.4, 11.4));
        painter.drawLine(QPointF(11.4, 4.6), QPointF(4.6, 11.4));
        painter.end();
        return QIcon(pm);
    }
};

/// @return the per-theme folder holding this theme's generated glyphs (created on demand).
QString glyphDir(ThemeManager::Mode mode) {
    const QString name = (mode == ThemeManager::Mode::Light) ? QStringLiteral("aluchop-glyphs-light")
                                                             : QStringLiteral("aluchop-glyphs-dark");
    return QDir::tempPath() + QLatin1Char('/') + name;
}

/// Writes @p svg to `<dir>/<name>`; failures are deliberately silent (the sheet degrades).
void writeGlyph(const QString& dir, const QString& name, const QString& svg) {
    QFile file(dir + QLatin1Char('/') + name);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(svg.toUtf8());
        file.close();
    }
}

// --- the header band that has to reach the scroll-bar gutter ------------------------------------
//
// A QHeaderView is only as wide as its view's *viewport*, but QAbstractScrollArea lays the vertical
// scroll bar out over the view's whole height — so the ~12 px gutter beside the header belongs to
// the QTableView itself, not to the header, and used to paint in the table's own (card) colour.
// The result was a white notch at every table's top-right corner, cut out of the tinted header band.
//
// There is no style-sheet pseudo-element for that corner (QTableCornerButton is the *top-left* one),
// so the band is painted underneath instead: a one-tile-wide strip exactly one header tall is
// generated here and repeated along the top of the table's own background. It is clipped by the
// table's 12 px radius, so the rounded top corners survive, and the header then draws over it —
// leaving nothing between the last column and the table's right edge but the same tint.

constexpr int kHeaderPadV = 11;      ///< Must equal the vertical padding of QHeaderView::section.
constexpr int kHeaderFontPx = 11;    ///< Must equal the font-size of QHeaderView::section.
constexpr int kHeaderStyleMargin = 8;   ///< PM_HeaderMargin, counted on both edges by Fusion.

/// @return the exact painted height of one header row, from the same metrics QHeaderView uses.
///
/// A one- or two-pixel error here would only ever show as a hairline inside a 12 px gutter, but the
/// number is derived rather than typed so it tracks the padding declared in the sheet below.
int headerBandHeight() {
    QFont f = QApplication::font();
    f.setPixelSize(kHeaderFontPx);
    f.setWeight(QFont::Bold);
    return QFontMetrics(f).height() + 2 * kHeaderPadV + kHeaderStyleMargin;
}

/// Writes the repeating header-band tile (band colour, then the header's own 1 px bottom rule).
void writeHeaderBand(const QString& dir, const QColor& band, const QColor& rule) {
    const int h = headerBandHeight();
    QPixmap tile(8, h);
    tile.fill(band);
    QPainter painter(&tile);
    painter.setPen(rule);
    painter.drawLine(0, h - 1, 8, h - 1);
    painter.end();
    tile.save(dir + QStringLiteral("/header-band.png"));
}

/// Regenerates every sub-control glyph in the colours of @p p.
/// @param headerTint the derived table-header band colour, so the generated strip and the
///                   `QHeaderView::section` rule can never drift apart.
/// @return the folder the glyphs were written to (also valid when a write failed).
QString buildGlyphs(const Palette& p, ThemeManager::Mode mode, const QColor& headerTint) {
    const QString dir = glyphDir(mode);
    QDir().mkpath(dir);

    const QString stroke = hex(p.textMuted);
    const QString strokeHot = hex(p.text);
    const QString onPrimary = hex(onColour(p.primary));
    const QString dot = hex(p.primary);

    const auto chevron = [](const QString& colour, const QString& points) {
        return QStringLiteral(
                   "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>"
                   "<polyline points='%1' fill='none' stroke='%2' stroke-width='2' "
                   "stroke-linecap='round' stroke-linejoin='round'/></svg>")
            .arg(points, colour);
    };

    writeGlyph(dir, QStringLiteral("chevron-down.svg"), chevron(stroke, QStringLiteral("4,6 8,10.5 12,6")));
    writeGlyph(dir, QStringLiteral("chevron-up.svg"), chevron(stroke, QStringLiteral("4,10 8,5.5 12,10")));
    writeGlyph(dir, QStringLiteral("chevron-right.svg"), chevron(stroke, QStringLiteral("6,4 10.5,8 6,12")));
    writeGlyph(dir, QStringLiteral("chevron-down-hot.svg"),
               chevron(strokeHot, QStringLiteral("4,6 8,10.5 12,6")));
    writeGlyph(dir, QStringLiteral("chevron-up-hot.svg"),
               chevron(strokeHot, QStringLiteral("4,10 8,5.5 12,10")));

    writeGlyph(dir, QStringLiteral("check.svg"),
               QStringLiteral(
                   "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>"
                   "<polyline points='3.5,8.5 6.5,11.5 12.5,4.8' fill='none' stroke='%1' "
                   "stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'/></svg>")
                   .arg(onPrimary));

    writeGlyph(dir, QStringLiteral("radio.svg"),
               QStringLiteral(
                   "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>"
                   "<circle cx='8' cy='8' r='4' fill='%1'/></svg>")
                   .arg(dot));

    writeHeaderBand(dir, headerTint, p.border);

    return dir;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Singleton plumbing
// ---------------------------------------------------------------------------------------------

ThemeManager::ThemeManager(QObject* parent) : QObject(parent) {}

/// @oop-concept Static Members :: a Meyers singleton — one theme for one process, constructed on
/// first use and destroyed at exit without any manual lifetime management
ThemeManager& ThemeManager::instance() {
    static ThemeManager s_instance;
    return s_instance;
}

const Palette& ThemeManager::palette() const noexcept {
    return (m_mode == Mode::Light) ? kLight : kDark;
}

void ThemeManager::setMode(Mode m) {
    if (m == m_mode) {
        return; // no-op: never emit a change that did not happen
    }
    m_mode = m;
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        apply(*app);
    }
    emit themeChanged();
}

void ThemeManager::toggle() {
    setMode(m_mode == Mode::Light ? Mode::Dark : Mode::Light);
}

void ThemeManager::apply(QApplication& app) {
    // Fusion is the only Qt style that honours the full style-sheet vocabulary identically on
    // every platform; the native macOS style silently ignores several sub-control rules, which
    // would leave the app looking half-themed. Installed once — re-installing resets palettes.
    static bool s_styleInstalled = false;
    if (!s_styleInstalled) {
        if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
            // Wrapped so the line-edit clear button is drawn in the design system's language
            // rather than as the platform's black disc — see AluChopStyle.
            QApplication::setStyle(new AluChopStyle(fusion));
        }
        s_styleInstalled = true;
    }

    const Palette& p = palette();

    // The QPalette is the safety net beneath the sheet: hand-painted widgets (charts, the
    // sidebar rail, splash artwork) and native popups read it directly.
    QPalette qp;
    qp.setColor(QPalette::Window, p.background);
    qp.setColor(QPalette::WindowText, p.text);
    qp.setColor(QPalette::Base, p.card);
    qp.setColor(QPalette::AlternateBase, mix(p.card, p.accent, 0.10));
    qp.setColor(QPalette::Text, p.text);
    qp.setColor(QPalette::PlaceholderText, p.textMuted);
    qp.setColor(QPalette::Button, p.card);
    qp.setColor(QPalette::ButtonText, p.text);
    qp.setColor(QPalette::BrightText, p.danger);
    qp.setColor(QPalette::Highlight, p.primary);
    // Not white-by-reflex: on the dark theme's pale sage highlight, white scores 3.0:1 and the
    // deep ink 5.9:1, so the selected row has to take whichever actually reads.
    qp.setColor(QPalette::HighlightedText, onColour(p.primary));
    qp.setColor(QPalette::ToolTipBase, p.card);
    qp.setColor(QPalette::ToolTipText, p.text);
    qp.setColor(QPalette::Link, p.primary);
    qp.setColor(QPalette::LinkVisited, p.secondary);
    qp.setColor(QPalette::Mid, p.border);
    qp.setColor(QPalette::Dark, mix(p.background, p.text, 0.20));
    qp.setColor(QPalette::Disabled, QPalette::Text, p.textMuted);
    qp.setColor(QPalette::Disabled, QPalette::ButtonText, p.textMuted);
    qp.setColor(QPalette::Disabled, QPalette::WindowText, p.textMuted);
    app.setPalette(qp);

    QFont base = app.font();
    // The platform UI font, resolved rather than guessed — see uiFontStack().
    const QString platformUi = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    if (!platformUi.isEmpty()) {
        base.setFamily(platformUi);
    }
    base.setPointSizeF(13.0);
    base.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(base);

    app.setStyleSheet(styleSheet());
}

// ---------------------------------------------------------------------------------------------
// The sheet itself
// ---------------------------------------------------------------------------------------------

QString ThemeManager::styleSheet() const {
    const Palette& p = palette();
    const bool light = (m_mode == Mode::Light);

    // Derived working surfaces — named once here so the sheet below reads as design intent
    // rather than as arithmetic.
    const QColor headerTint = mix(p.card, p.accent, light ? 0.22 : 0.10);
    const QColor zebra = mix(p.card, p.accent, light ? 0.10 : 0.06);
    const QColor railTint = mix(p.card, p.accent, light ? 0.14 : 0.05);
    // With the sliding 4 px indicator gone the pill is the *only* thing that says "you are here",
    // so it carries three simultaneous steps away from the rail behind it: a firmer sage fill, a
    // hairline of the same family, and a brighter, heavier label. Hover sits deliberately between
    // the two so the rail still answers the pointer without impersonating the selection.
    const QColor railHover = mix(p.card, p.accent, light ? 0.28 : 0.11);
    const QColor railActive = mix(p.card, p.accent, light ? 0.50 : 0.22);
    const QColor railActiveLine = mix(p.card, p.accent, light ? 0.72 : 0.34);
    const QColor railActiveText = light ? mix(p.primary, p.text, 0.45) : mix(p.accent, kPaper, 0.35);
    const QColor handle = mix(p.background, p.text, light ? 0.20 : 0.26);
    const QColor handleHot = mix(p.background, p.primary, 0.55);
    const QColor wash = mix(p.background, p.accent, light ? 0.35 : 0.10);

    // --- filled controls -----------------------------------------------------------------
    // Light darkens on interaction, dark lightens: in both directions the fill moves *away*
    // from its own ink, so a hover can never be the state that loses the label.
    const QColor primaryHover = light ? mix(p.primary, p.text, 0.14) : mix(p.primary, kPaper, 0.16);
    const QColor primaryPressed =
        light ? mix(p.primary, p.text, 0.26) : mix(p.primary, p.background, 0.16);
    // SPEC §1 fixes #D16464 as *the* danger colour; white on it is only 3.7:1, so the filled
    // destructive button uses a deepened member of the same hue while alerts, chips and text keep
    // the specified value.
    const QColor dangerFill = light ? mix(p.danger, p.text, 0.16) : p.danger;
    const QColor dangerHover = light ? mix(p.danger, p.text, 0.30) : mix(p.danger, kPaper, 0.14);
    const QColor dangerPressed =
        light ? mix(p.danger, p.text, 0.40) : mix(p.danger, p.background, 0.14);

    // --- the brand colour used as *text* on a surface, not as a fill ----------------------
    const QColor primaryText = light ? mix(p.primary, p.text, 0.18) : mix(p.primary, kPaper, 0.30);
    const QColor dangerText = light ? mix(p.danger, p.text, 0.22) : p.danger;
    const QColor successText = light ? mix(p.success, p.text, 0.32) : p.success;
    // Column headings sit on the tinted header band, not on the card, so the muted ink needs one
    // more step of contrast there to stay above 4.5:1 at 11 px.
    const QColor headerText = light ? mix(p.textMuted, p.text, 0.12) : p.textMuted;
    // The alert labels print their colour *on their own wash*, which lifts the background towards
    // the very hue the text is made of; both sides therefore move apart again here.
    const QColor dangerOnWash = light ? mix(p.danger, p.text, 0.32) : mix(p.danger, kPaper, 0.18);
    const QColor successOnWash = light ? mix(p.success, p.text, 0.40) : mix(p.success, kPaper, 0.10);

    // Written last, because the header-band strip is generated from headerTint above.
    const QString glyphs = buildGlyphs(p, m_mode, headerTint);

    QString qss = QStringLiteral(R"QSS(
/* =====================================================================================
   AluChop — generated stylesheet (@modeName@)
   Sage Green design system · SPEC §1 · generated at runtime by gui::ThemeManager
   ===================================================================================== */

/* --- 1. Foundations ---------------------------------------------------------------- */
* {
    outline: 0;
}

QWidget {
    color: @text@;
    font-family: @fontStack@;
    font-size: 13px;
}

QMainWindow, QDialog {
    background-color: @background@;
}

QMainWindow::separator {
    background-color: @border@;
    width: 1px;
    height: 1px;
}

QLabel, QCheckBox, QRadioButton, QGroupBox {
    background: transparent;
}

QToolTip {
    background-color: @card@;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 8px;
    padding: 6px 10px;
    font-size: 12px;
}

QScrollArea, QStackedWidget, QSplitter {
    background: transparent;
    border: none;
}

QSplitter::handle {
    background-color: @border@;
}
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical   { height: 1px; }

QFrame[frameShape="4"], QFrame[frameShape="5"] {   /* HLine / VLine separators */
    background-color: @border@;
    border: none;
    max-height: 1px;
}

/* --- 2. Typography ----------------------------------------------------------------- */
#pageTitle {
    font-size: 27px;
    font-weight: 700;
    color: @text@;
    padding: 0px;
}

#pageSubtitle, #mutedLabel {
    font-size: 12px;
    color: @textMuted@;
}

#sectionTitle {
    font-size: 14px;
    font-weight: 700;
    color: @text@;
}

#brandLabel {
    font-size: 21px;
    font-weight: 800;
    color: @primaryText@;
}

#brandTagline {
    font-size: 10px;
    font-weight: 600;
    color: @textMuted@;
}

#footerCredit {
    font-size: 11px;
    color: @textMuted@;
}

#errorLabel {
    font-size: 12px;
    font-weight: 600;
    color: @dangerOnWash@;
    background-color: @dangerWash@;
    border: 1px solid @dangerLine@;
    border-radius: 8px;
    padding: 8px 12px;
}

#successLabel {
    font-size: 12px;
    font-weight: 600;
    color: @successOnWash@;
    background-color: @successWash@;
    border: 1px solid @successLine@;
    border-radius: 8px;
    padding: 8px 12px;
}

/* --- 3. Surfaces ------------------------------------------------------------------- */
#card {
    background-color: @card@;
    border: 1px solid @border@;
    border-radius: 14px;
}

#glassPanel {
    background-color: @glass@;
    border: 1px solid @glassLine@;
    border-radius: 18px;
}

#statCard {
    background-color: @card@;
    border: 1px solid @border@;
    border-radius: 16px;
}

#statCardIcon {
    background-color: @accentSoft@;
    border-radius: 18px;
}

#statCardTitle {
    font-size: 11px;
    font-weight: 700;
    color: @textMuted@;
}

#statCardValue {
    font-size: 30px;
    font-weight: 800;
    color: @text@;
}

#statCardDelta {
    font-size: 12px;
    font-weight: 600;
    color: @textMuted@;
}
#statCardDelta[trend="up"]   { color: @successText@; }
#statCardDelta[trend="down"] { color: @dangerText@; }

#emptyState {
    background: transparent;
    border: 1px dashed @border@;
    border-radius: 14px;
}

#emptyStateTitle {
    font-size: 15px;
    font-weight: 700;
    color: @textMuted@;
}

#emptyStateHint {
    font-size: 12px;
    color: @textMuted@;
}

#loadingOverlay {
    background-color: @veil@;
    border-radius: 14px;
}

#loadingOverlayText {
    font-size: 13px;
    font-weight: 600;
    color: @text@;
}

#appShell, #loginBackdrop {
    background-color: @background@;
    border: none;
}

#commandBar {
    background-color: @card@;
    border: none;
}

#loginBackdrop {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                stop:0 @background@, stop:0.55 @wash@, stop:1 @background@);
}

/* --- 4. Buttons -------------------------------------------------------------------- */
/* Every filled button takes its label colour from an on-colour that was measured against its
   own fill (see onColour() in this file) instead of being assumed to be white. */
QPushButton {
    background-color: @card@;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 10px;
    padding: 8px 18px;
    font-size: 13px;
    font-weight: 600;
    min-height: 20px;
}
QPushButton:hover    { background-color: @hover@; border-color: @accent@; }
QPushButton:pressed  { background-color: @accentSoft@; border-color: @accent@; }
QPushButton:disabled { background-color: @background@; color: @textMuted@; border-color: @border@; }

QPushButton#primaryButton {
    background-color: @primary@;
    color: @onPrimary@;
    border: 1px solid @primary@;
    padding: 9px 22px;
}
QPushButton#primaryButton:hover {
    background-color: @primaryHover@;
    border-color: @primaryHover@;
    color: @onPrimary@;
}
QPushButton#primaryButton:pressed {
    background-color: @primaryPressed@;
    border-color: @primaryPressed@;
    color: @onPrimary@;
}
QPushButton#primaryButton:disabled { background-color: @border@; border-color: @border@; color: @textMuted@; }

QPushButton#ghostButton {
    background-color: transparent;
    color: @primaryText@;
    border: 1px solid @primary@;
}
QPushButton#ghostButton:hover    { background-color: @accentSoft@; color: @primaryText@; }
QPushButton#ghostButton:pressed  { background-color: @accent@; color: @onAccent@; }
QPushButton#ghostButton:disabled { color: @textMuted@; border-color: @border@; background: transparent; }

QPushButton#dangerButton {
    background-color: @dangerFill@;
    color: @onDanger@;
    border: 1px solid @dangerFill@;
}
QPushButton#dangerButton:hover {
    background-color: @dangerHover@;
    border-color: @dangerHover@;
    color: @onDanger@;
}
QPushButton#dangerButton:pressed {
    background-color: @dangerPressed@;
    border-color: @dangerPressed@;
    color: @onDanger@;
}
QPushButton#dangerButton:disabled { background-color: @border@; border-color: @border@; color: @textMuted@; }

/* The shell's global-search affordance is a button that must read as a field. */
QPushButton#searchBar {
    background-color: @background@;
    color: @textMuted@;
    border: 1px solid @border@;
    border-radius: 19px;
    padding: 8px 18px;
    font-weight: 500;
    text-align: left;
}
QPushButton#searchBar:hover {
    background-color: @hover@;
    border-color: @accent@;
    color: @text@;
}
QPushButton#searchBar:pressed { background-color: @accentSoft@; }

QPushButton#linkButton {
    background: transparent;
    border: none;
    color: @primaryText@;
    font-weight: 600;
    padding: 4px 2px;
}
QPushButton#linkButton:hover   { color: @primary@; }
QPushButton#linkButton:pressed { color: @primaryPressed@; }

QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 9px;
    padding: 6px;
    color: @text@;
}
QToolButton:hover   { background-color: @hover@; }
QToolButton:pressed { background-color: @accentSoft@; }

QDialogButtonBox QPushButton { min-width: 84px; }

/* --- 5. Navigation rail ------------------------------------------------------------ */
#sidebar {
    background-color: @rail@;
    border-right: 1px solid @border@;
}

/* The current page is announced by the rounded pill alone — no sliding bar down the edge. For
   that to read at a glance the pill has to move three things at once: fill, hairline and label. */
QToolButton#sidebarButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 12px;
    color: @textMuted@;
    font-size: 13px;
    font-weight: 600;
    padding: 10px 12px;
    text-align: left;
}
QToolButton#sidebarButton:hover {
    background-color: @railHover@;
    border-color: transparent;
    color: @text@;
}
QToolButton#sidebarButton:checked {
    background-color: @railActive@;
    border-color: @railActiveLine@;
    color: @railActiveText@;
    font-weight: 700;
}
QToolButton#sidebarButton:checked:hover { background-color: @railActive@; color: @railActiveText@; }

/* The Ctrl+K affordance at the foot of the rail: a keycap chip plus one short line, sized so it
   never has to wrap inside the 204 px rail. */
#sidebarHint {
    background-color: @hover@;
    border: 1px solid @border@;
    border-radius: 11px;
}

#sidebarHintText {
    font-size: 11px;
    font-weight: 500;
    color: @textMuted@;
    background: transparent;
}

#kbdChip {
    font-size: 10px;
    font-weight: 700;
    color: @text@;
    background-color: @card@;
    border: 1px solid @border@;
    border-radius: 6px;
    padding: 3px 7px;
}

/* --- 6. Text inputs ---------------------------------------------------------------- */
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox,
QDateEdit, QTimeEdit, QDateTimeEdit, QComboBox {
    background-color: @card@;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 10px;
    padding: 7px 12px;
    min-height: 20px;
    selection-background-color: @accent@;
    selection-color: @onAccent@;
}

QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus,
QDateEdit:focus, QTimeEdit:focus, QDateTimeEdit:focus, QComboBox:focus, QComboBox:on {
    border: 2px solid @primary@;
    padding: 6px 11px;
}

QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled, QSpinBox:disabled,
QDoubleSpinBox:disabled, QDateEdit:disabled, QComboBox:disabled {
    background-color: @background@;
    color: @textMuted@;
}

QLineEdit[readOnly="true"] {
    background-color: @background@;
    color: @textMuted@;
}

/* The icon itself comes from AluChopStyle (a style-sheet selector cannot single out the clear
   button); this rule only gives it the surrounding chrome — no frame, and a soft hover wash
   instead of the platform's pressed-disc look. */
QLineEdit > QToolButton {
    background: transparent;
    border: none;
    border-radius: 8px;
    padding: 0px;
    margin: 0px 2px 0px 0px;
}
QLineEdit > QToolButton:hover   { background-color: @hover@; }
QLineEdit > QToolButton:pressed { background-color: @accentSoft@; }

#searchBar {
    border-radius: 19px;
    padding-left: 16px;
    padding-right: 10px;
}

#paletteInput {
    background: transparent;
    border: none;
    border-bottom: 1px solid @glassLine@;
    border-radius: 0px;
    font-size: 18px;
    font-weight: 500;
    padding: 16px 20px;
}
#paletteInput:focus {
    border: none;
    border-bottom: 1px solid @primary@;
    padding: 16px 20px;
}

/* --- 7. Combo boxes ---------------------------------------------------------------- */
QComboBox { padding-right: 30px; }
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: center right;
    width: 26px;
    border: none;
    background: transparent;
}
QComboBox::down-arrow {
    image: url(@glyphs@/chevron-down.svg);
    width: 14px;
    height: 14px;
}
QComboBox::down-arrow:on, QComboBox::down-arrow:hover {
    image: url(@glyphs@/chevron-down-hot.svg);
}
QComboBox QAbstractItemView {
    background-color: @card@;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 10px;
    padding: 5px;
    outline: 0;
    selection-background-color: @accentSoft@;
    selection-color: @text@;
}
QComboBox QAbstractItemView::item {
    min-height: 28px;
    padding: 4px 10px;
    border-radius: 7px;
}

/* --- 8. Spin boxes and date editors ------------------------------------------------ */
/* A date editor with a calendar popup is painted by Qt as a *combo box*, so it needs the combo's
   sub-controls, not the spin box's — that mismatch is why the Reports date fields used to render
   with a raw Fusion drop-down next to a fully themed QComboBox. Both shapes are styled here so
   the two controls are indistinguishable. */
QSpinBox, QDoubleSpinBox, QDateEdit, QTimeEdit, QDateTimeEdit { padding-right: 28px; }

QDateEdit::drop-down, QTimeEdit::drop-down, QDateTimeEdit::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: center right;
    width: 26px;
    border: none;
    background: transparent;
}

QSpinBox::up-button, QDoubleSpinBox::up-button, QDateEdit::up-button,
QTimeEdit::up-button, QDateTimeEdit::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
    width: 22px;
    margin: 4px 5px 0px 0px;
    border: none;
    border-radius: 6px;
    background: transparent;
}
QSpinBox::down-button, QDoubleSpinBox::down-button, QDateEdit::down-button,
QTimeEdit::down-button, QDateTimeEdit::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
    width: 22px;
    margin: 0px 5px 4px 0px;
    border: none;
    border-radius: 6px;
    background: transparent;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover, QDateEdit::up-button:hover,
QTimeEdit::up-button:hover, QDateTimeEdit::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover, QDateEdit::down-button:hover,
QTimeEdit::down-button:hover, QDateTimeEdit::down-button:hover {
    background-color: @hover@;
}
QSpinBox::up-button:pressed, QDoubleSpinBox::up-button:pressed, QDateEdit::up-button:pressed,
QTimeEdit::up-button:pressed, QDateTimeEdit::up-button:pressed,
QSpinBox::down-button:pressed, QDoubleSpinBox::down-button:pressed,
QDateEdit::down-button:pressed, QTimeEdit::down-button:pressed,
QDateTimeEdit::down-button:pressed {
    background-color: @accentSoft@;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow, QDateEdit::up-arrow,
QTimeEdit::up-arrow, QDateTimeEdit::up-arrow {
    image: url(@glyphs@/chevron-up.svg);
    width: 11px;
    height: 11px;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow, QDateEdit::down-arrow,
QTimeEdit::down-arrow, QDateTimeEdit::down-arrow {
    image: url(@glyphs@/chevron-down.svg);
    width: 11px;
    height: 11px;
}
QSpinBox::up-arrow:hover, QDoubleSpinBox::up-arrow:hover, QDateEdit::up-arrow:hover,
QTimeEdit::up-arrow:hover, QDateTimeEdit::up-arrow:hover {
    image: url(@glyphs@/chevron-up-hot.svg);
}
QSpinBox::down-arrow:hover, QDoubleSpinBox::down-arrow:hover, QDateEdit::down-arrow:hover,
QTimeEdit::down-arrow:hover, QDateTimeEdit::down-arrow:hover {
    image: url(@glyphs@/chevron-down-hot.svg);
}
/* A date editor in calendar-popup mode paints one combo-style arrow instead of two spin arrows;
   it must be the same 14 px chevron the QComboBox next to it uses. */
QDateEdit::down-arrow:on, QTimeEdit::down-arrow:on, QDateTimeEdit::down-arrow:on {
    image: url(@glyphs@/chevron-down-hot.svg);
    width: 14px;
    height: 14px;
}

QCalendarWidget QWidget { background-color: @card@; }
QCalendarWidget QAbstractItemView {
    background-color: @card@;
    color: @text@;
    selection-background-color: @primary@;
    selection-color: @onPrimary@;
    outline: 0;
}
QCalendarWidget QToolButton { color: @text@; font-weight: 600; }

/* --- 9. Tables --------------------------------------------------------------------- */
/* `background-image` is the header band described next to writeHeaderBand(): one header-tall tile
   repeated along the top of the *view* (not the viewport), so the tint and its bottom rule run the
   full width of the table and the scroll-bar gutter can no longer punch a white notch out of the
   top-right corner. It is clipped by the border-radius below, so the rounded corners survive. */
QTableView, QTableWidget, QTreeView {
    background-color: @card@;
    background-image: url(@glyphs@/header-band.png);
    background-repeat: repeat-x;
    background-position: top left;
    background-attachment: fixed;
    alternate-background-color: @zebra@;
    color: @text@;
    border: none;
    border-radius: 12px;
    gridline-color: transparent;
    selection-background-color: @accentSoft@;
    selection-color: @text@;
    outline: 0;
}

QTableView::item, QTreeView::item {
    border: none;
    padding: 6px 12px;
}
QTableView::item:hover    { background-color: @hover@; }
QTableView::item:selected { background-color: @accentSoft@; color: @text@; }

QHeaderView { background: transparent; border: none; }
QHeaderView::section {
    background-color: @headerTint@;
    color: @headerText@;
    border: none;
    border-bottom: 1px solid @border@;
    padding: 11px 12px;
    font-size: 11px;
    font-weight: 700;
}
QHeaderView::section:hover { color: @text@; }
QHeaderView::section:first { border-top-left-radius: 12px; }
QHeaderView::section:last  { border-top-right-radius: 12px; }
QHeaderView::down-arrow {
    image: url(@glyphs@/chevron-down.svg);
    width: 11px; height: 11px;
    subcontrol-position: center right;
    right: 8px;
}
QHeaderView::up-arrow {
    image: url(@glyphs@/chevron-up.svg);
    width: 11px; height: 11px;
    subcontrol-position: center right;
    right: 8px;
}
/* Both scroll-area corners used to fall through to Fusion and paint as bare grey squares — one
   above the vertical header, one between the two scroll bars, where it reads as a stray
   checkbox. Neither carries any meaning here, so both dissolve into the surface. */
QTableCornerButton::section {
    background-color: @headerTint@;
    border: none;
    border-bottom: 1px solid @border@;
    border-top-left-radius: 12px;
}
QAbstractScrollArea::corner {
    background: transparent;
    border: none;
}

/* --- 10. Lists --------------------------------------------------------------------- */
QListWidget, QListView {
    background-color: @card@;
    color: @text@;
    border: none;
    border-radius: 12px;
    padding: 5px;
    outline: 0;
}
QListWidget::item, QListView::item {
    border-radius: 8px;
    padding: 9px 12px;
    margin: 1px 0px;
}
QListWidget::item:hover    { background-color: @hover@; }
QListWidget::item:selected { background-color: @accentSoft@; color: @text@; }

#paletteResults {
    background: transparent;
    padding: 10px;
}
#paletteResults::item {
    padding: 0px;              /* each row supplies its own insets — see CommandPalette */
    margin: 1px 0px;
    border-radius: 10px;
}

/* One command-palette row: a prominent name and a quiet, right-aligned trailing detail, so a
   result reads as a hierarchy instead of as two tab-separated words. */
#paletteSectionHeader {
    font-size: 10px;
    font-weight: 800;
    color: @textMuted@;
    background: transparent;
    padding: 2px;
}
#paletteRowName {
    font-size: 14px;
    font-weight: 600;
    color: @text@;
    background: transparent;
}
#paletteRowMeta {
    font-size: 11px;
    font-weight: 500;
    color: @textMuted@;
    background: transparent;
}

/* --- 11. Scroll bars --------------------------------------------------------------- */
/* Slim, rounded and inset far enough from the ends that a bar never crosses the 12–16 px radius
   of the card it is scrolling. The track is fully transparent: only the handle is ever visible. */
QScrollBar:vertical {
    background: transparent;
    border: none;
    width: 12px;
    margin: 8px 3px 8px 3px;
}
QScrollBar::handle:vertical {
    background-color: @handle@;
    border-radius: 3px;
    min-height: 44px;
}
QScrollBar::handle:vertical:hover   { background-color: @handleHot@; }
QScrollBar::handle:vertical:pressed { background-color: @primary@; }

QScrollBar:horizontal {
    background: transparent;
    border: none;
    height: 12px;
    margin: 3px 8px 3px 8px;
}
QScrollBar::handle:horizontal {
    background-color: @handle@;
    border-radius: 3px;
    min-width: 44px;
}
QScrollBar::handle:horizontal:hover   { background-color: @handleHot@; }
QScrollBar::handle:horizontal:pressed { background-color: @primary@; }

QScrollBar::add-line, QScrollBar::sub-line {
    width: 0px; height: 0px; border: none; background: transparent;
}
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* --- 12. Tabs ---------------------------------------------------------------------- */
QTabWidget::pane {
    background-color: @card@;
    border: 1px solid @border@;
    border-radius: 12px;
    top: -1px;
}
QTabBar { background: transparent; }
QTabBar::tab {
    background: transparent;
    color: @textMuted@;
    border: 1px solid transparent;
    border-top-left-radius: 10px;
    border-top-right-radius: 10px;
    padding: 9px 20px;
    margin-right: 4px;
    font-weight: 600;
}
QTabBar::tab:hover { color: @text@; background-color: @hover@; }
QTabBar::tab:selected {
    background-color: @card@;
    color: @primaryText@;
    border-color: @border@;
    border-bottom-color: @card@;
}

/* --- 13. Menus --------------------------------------------------------------------- */
QMenuBar {
    background-color: @card@;
    color: @text@;
    border-bottom: 1px solid @border@;
}
QMenuBar::item {
    background: transparent;
    padding: 6px 12px;
    border-radius: 8px;
}
QMenuBar::item:selected { background-color: @hover@; }

QMenu {
    background-color: @card@;
    border: 1px solid @border@;
    border-radius: 12px;
    padding: 6px;
}
QMenu::item {
    padding: 8px 26px 8px 16px;
    border-radius: 8px;
}
QMenu::item:selected { background-color: @accentSoft@; color: @text@; }
QMenu::item:disabled { color: @textMuted@; }
QMenu::separator {
    height: 1px;
    background-color: @border@;
    margin: 6px 10px;
}
QMenu::right-arrow {
    image: url(@glyphs@/chevron-right.svg);
    width: 12px; height: 12px;
}

/* --- 14. Choice controls ----------------------------------------------------------- */
QCheckBox, QRadioButton {
    spacing: 9px;
    color: @text@;
}
QCheckBox::indicator, QRadioButton::indicator {
    width: 18px;
    height: 18px;
}
QCheckBox::indicator {
    border: 2px solid @border@;
    border-radius: 6px;
    background-color: @card@;
}
QCheckBox::indicator:hover { border-color: @primary@; }
QCheckBox::indicator:checked {
    background-color: @primary@;
    border-color: @primary@;
    image: url(@glyphs@/check.svg);
}
QCheckBox::indicator:disabled { background-color: @background@; border-color: @border@; }

QRadioButton::indicator {
    border: 2px solid @border@;
    border-radius: 9px;
    background-color: @card@;
}
QRadioButton::indicator:hover   { border-color: @primary@; }
QRadioButton::indicator:checked {
    border-color: @primary@;
    image: url(@glyphs@/radio.svg);
}

/* --- 15. Group boxes and progress -------------------------------------------------- */
QGroupBox {
    background-color: @card@;
    border: 1px solid @border@;
    border-radius: 12px;
    margin-top: 14px;
    padding: 18px 14px 14px 14px;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 14px;
    padding: 0px 6px;
    color: @textMuted@;
    font-size: 11px;
    font-weight: 700;
}

/* Track and fill share one radius and carry no padding, so the fill cannot leave a seam or a
   one-pixel step where it meets the track. */
QProgressBar {
    background-color: @hover@;
    border: none;
    border-radius: 4px;
    min-height: 8px;
    max-height: 8px;
    padding: 0px;
    text-align: center;
    color: @textMuted@;
}
QProgressBar::chunk {
    background-color: @primary@;
    border-radius: 4px;
    margin: 0px;
}

/* The splash card is the sage gradient in both themes, so its indicator is always drawn in the
   ink that gradient takes — not in a palette colour that would vanish into it.

   The bar is deliberately *determinate* (SplashScreen drives it with a QPropertyAnimation). Qt's
   busy indicator wraps its chunk around the ends of the groove, which drew as two white slabs with
   the track showing between them — one pill-capped, one sliced square at the track edge. A single
   growing fill has no wrap point, and with radius = half the 6 px height on both the groove and the
   chunk (and no margin on either) the two ends stay identically capped at every width. */
#splashProgress {
    background-color: rgba(255,255,255,0.22);
    border: none;
    border-radius: 3px;
    min-height: 6px;
    max-height: 6px;
    padding: 0px;
    margin: 0px;
}
#splashProgress::chunk {
    background-color: rgba(255,255,255,0.95);
    border-radius: 3px;
    margin: 0px;
}
#splashStatus {
    color: rgba(255,255,255,0.95);
    font-size: 12px;
    font-weight: 600;
    background: transparent;
}

/* --- 16. Notifications and the command palette ------------------------------------- */
#toast {
    background-color: @card@;
    border: 1px solid @border@;
    border-left: 4px solid @primary@;
    border-radius: 12px;
}
#toast[level="1"] { border-left-color: @success@; }
#toast[level="2"] { border-left-color: @accent@; }
#toast[level="3"] { border-left-color: @danger@; }

#toastTitle {
    font-size: 13px;
    font-weight: 700;
    color: @text@;
}
#toastMessage {
    font-size: 12px;
    color: @textMuted@;
}

#commandPalette {
    background-color: @glass@;
    border: 1px solid @glassLine@;
    border-radius: 18px;
}
#paletteHint {
    font-size: 11px;
    color: @textMuted@;
    padding: 6px 18px 12px 18px;
}

/* --- 17. Settings palette specimen -------------------------------------------------- */
/* These chips are samples of the palette, not controls: each one shows its own colour with the
   ink that is legible on it. They are QLabels precisely so no widget state (disabled, hover) can
   ever repaint a swatch in a colour other than the one it is advertising. */
#swatchPrimary, #swatchSecondary, #swatchAccent, #swatchDanger, #swatchSuccess {
    font-size: 11px;
    font-weight: 700;
    border-radius: 9px;
    padding: 9px 14px;
    min-height: 18px;
}
#swatchPrimary   { background-color: @primary@;    color: @onPrimary@;   border: 1px solid @primary@; }
#swatchSecondary { background-color: @secondary@;  color: @onSecondary@; border: 1px solid @secondary@; }
#swatchAccent    { background-color: @accent@;     color: @onAccent@;    border: 1px solid @accent@; }
#swatchDanger    { background-color: @dangerFill@; color: @onDanger@;    border: 1px solid @dangerFill@; }
#swatchSuccess   { background-color: @success@;    color: @onSuccess@;   border: 1px solid @success@; }

#backupList::item {
    padding: 10px 12px;
    border-radius: 9px;
}

/* --- 18. Receipt preview ------------------------------------------------------------ */
/* A printed receipt is column-aligned monospace text; rendering it in the UI font would
   misalign every price. */
#receiptView {
    font-family: @monoStack@;
    font-size: 12px;
    background-color: @card@;
    border: 1px solid @border@;
    border-radius: 12px;
    padding: 14px 16px;
}

#pageStack {
    background: transparent;
}

/* --- 19. Charts -------------------------------------------------------------------- */
QChartView {
    background: transparent;
    border: none;
}
)QSS");

    // -----------------------------------------------------------------------------------------
    // Token substitution. Every @token@ above must appear in this table.
    //
    /// @oop-concept STL (vector) + std::pair :: the palette is projected onto the sheet through a
    /// simple ordered table, which keeps the QSS above readable as design rather than as string
    /// concatenation
    // -----------------------------------------------------------------------------------------
    const QVector<std::pair<QString, QString>> tokens{
        {QStringLiteral("@modeName@"), light ? QStringLiteral("Light") : QStringLiteral("Dark")},
        {QStringLiteral("@fontStack@"), uiFontStack()},
        {QStringLiteral("@monoStack@"), monoFontStack()},
        {QStringLiteral("@primaryText@"), hex(primaryText)},
        {QStringLiteral("@primaryHover@"), hex(primaryHover)},
        {QStringLiteral("@primaryPressed@"), hex(primaryPressed)},
        {QStringLiteral("@primary@"), hex(p.primary)},
        {QStringLiteral("@secondary@"), hex(p.secondary)},
        {QStringLiteral("@accentSoft@"), rgba(p.accent, light ? 0.34 : 0.22)},
        {QStringLiteral("@accent@"), hex(p.accent)},
        {QStringLiteral("@background@"), hex(p.background)},
        {QStringLiteral("@card@"), hex(p.card)},
        {QStringLiteral("@border@"), hex(p.border)},
        {QStringLiteral("@successWash@"), rgba(p.success, 0.12)},
        {QStringLiteral("@successLine@"), rgba(p.success, 0.45)},
        {QStringLiteral("@successOnWash@"), hex(successOnWash)},
        {QStringLiteral("@successText@"), hex(successText)},
        {QStringLiteral("@success@"), hex(p.success)},
        {QStringLiteral("@dangerFill@"), hex(dangerFill)},
        {QStringLiteral("@dangerHover@"), hex(dangerHover)},
        {QStringLiteral("@dangerPressed@"), hex(dangerPressed)},
        {QStringLiteral("@dangerWash@"), rgba(p.danger, 0.12)},
        {QStringLiteral("@dangerLine@"), rgba(p.danger, 0.45)},
        {QStringLiteral("@dangerOnWash@"), hex(dangerOnWash)},
        {QStringLiteral("@dangerText@"), hex(dangerText)},
        {QStringLiteral("@danger@"), hex(p.danger)},
        {QStringLiteral("@textMuted@"), hex(p.textMuted)},
        {QStringLiteral("@text@"), hex(p.text)},
        {QStringLiteral("@onPrimary@"), hex(onColour(p.primary))},
        {QStringLiteral("@onSecondary@"), hex(onColour(p.secondary))},
        {QStringLiteral("@onAccent@"), hex(onColour(p.accent))},
        {QStringLiteral("@onDanger@"), hex(onColour(dangerFill))},
        {QStringLiteral("@onSuccess@"), hex(onColour(p.success))},
        {QStringLiteral("@hover@"), hex(p.hover)},
        {QStringLiteral("@headerTint@"), hex(headerTint)},
        {QStringLiteral("@headerText@"), hex(headerText)},
        {QStringLiteral("@zebra@"), hex(zebra)},
        {QStringLiteral("@railActiveLine@"), hex(railActiveLine)},
        {QStringLiteral("@railActiveText@"), hex(railActiveText)},
        {QStringLiteral("@railActive@"), hex(railActive)},
        {QStringLiteral("@railHover@"), hex(railHover)},
        {QStringLiteral("@rail@"), hex(railTint)},
        {QStringLiteral("@handleHot@"), hex(handleHot)},
        {QStringLiteral("@handle@"), hex(handle)},
        {QStringLiteral("@wash@"), hex(wash)},
        {QStringLiteral("@glassLine@"), rgba(light ? p.card : p.accent, light ? 0.90 : 0.20)},
        {QStringLiteral("@glass@"), rgba(p.card, light ? 0.86 : 0.90)},
        {QStringLiteral("@veil@"), rgba(p.background, 0.88)},
        {QStringLiteral("@glyphs@"), glyphs},
    };

    for (const auto& token : tokens) {
        qss.replace(token.first, token.second);
    }
    return qss;
}

} // namespace aluchop::gui

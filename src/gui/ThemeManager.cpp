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
#include <QIODevice>
#include <QPalette>
#include <QStyleFactory>
#include <QVector>
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
    QColor(110, 127, 110),   // textMuted   #6E7F6E  (derived)
    QColor(237, 242, 233),   // hover       #EDF2E9  (derived)
    QColor(31, 45, 31, 77)   // shadow      text @ 30 %
};

/// @oop-concept Constant Objects :: the dark theme keeps the same hues, never pure black
const Palette ThemeManager::kDark{
    QColor(126, 155, 132),   // primary     #7E9B84
    QColor(93, 122, 102),    // secondary   #5D7A66
    QColor(168, 195, 161),   // accent      #A8C3A1
    QColor(20, 26, 21),      // background  #141A15
    QColor(28, 36, 29),      // card        #1C241D
    QColor(42, 53, 41),      // border      #2A3529
    QColor(111, 191, 119),   // success     #6FBF77
    QColor(224, 122, 122),   // danger      #E07A7A
    QColor(228, 235, 226),   // text        #E4EBE2
    QColor(143, 160, 141),   // textMuted   #8FA08D
    QColor(35, 45, 36),      // hover       #232D24
    QColor(0, 0, 0, 102)     // shadow      black @ 40 %
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

// ---------------------------------------------------------------------------------------------
// Generated sub-control artwork (see the file-level note).
// ---------------------------------------------------------------------------------------------

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

/// Regenerates every sub-control glyph in the colours of @p p.
/// @return the folder the glyphs were written to (also valid when a write failed).
QString buildGlyphs(const Palette& p, ThemeManager::Mode mode) {
    const QString dir = glyphDir(mode);
    QDir().mkpath(dir);

    const QString stroke = hex(p.textMuted);
    const QString onPrimary = QStringLiteral("#FFFFFF");
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
            QApplication::setStyle(fusion);
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
    qp.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
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
    const QString glyphs = buildGlyphs(p, m_mode);
    const bool light = (m_mode == Mode::Light);

    // Derived working surfaces — named once here so the sheet below reads as design intent
    // rather than as arithmetic.
    const QColor headerTint = mix(p.card, p.accent, light ? 0.22 : 0.10);
    const QColor zebra = mix(p.card, p.accent, light ? 0.10 : 0.06);
    const QColor railTint = mix(p.card, p.accent, light ? 0.14 : 0.05);
    const QColor railActive = mix(p.card, p.accent, light ? 0.42 : 0.18);
    const QColor handle = mix(p.background, p.text, light ? 0.18 : 0.28);
    const QColor handleHot = mix(p.background, p.primary, 0.55);
    const QColor primaryDeep = light ? p.primary.darker(118) : p.primary.darker(112);
    const QColor dangerDeep = p.danger.darker(115);
    const QColor wash = mix(p.background, p.accent, light ? 0.35 : 0.10);

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
    font-family: "SF Pro Text", "Helvetica Neue", "Segoe UI", "Inter", "Noto Sans", sans-serif;
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
    color: @primary@;
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
    color: @danger@;
    background-color: @dangerWash@;
    border: 1px solid @dangerLine@;
    border-radius: 8px;
    padding: 8px 12px;
}

#successLabel {
    font-size: 12px;
    font-weight: 600;
    color: @success@;
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
#statCardDelta[trend="up"]   { color: @success@; }
#statCardDelta[trend="down"] { color: @danger@; }

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
    color: #FFFFFF;
    border: 1px solid @primary@;
    padding: 9px 22px;
}
QPushButton#primaryButton:hover    { background-color: @secondary@; border-color: @secondary@; }
QPushButton#primaryButton:pressed  { background-color: @primaryDeep@; border-color: @primaryDeep@; }
QPushButton#primaryButton:disabled { background-color: @border@; border-color: @border@; color: @textMuted@; }

QPushButton#ghostButton {
    background-color: transparent;
    color: @primary@;
    border: 1px solid @primary@;
}
QPushButton#ghostButton:hover    { background-color: @accentSoft@; }
QPushButton#ghostButton:pressed  { background-color: @accent@; color: @text@; }
QPushButton#ghostButton:disabled { color: @textMuted@; border-color: @border@; background: transparent; }

QPushButton#dangerButton {
    background-color: @danger@;
    color: #FFFFFF;
    border: 1px solid @danger@;
}
QPushButton#dangerButton:hover    { background-color: @dangerDeep@; border-color: @dangerDeep@; }
QPushButton#dangerButton:pressed  { background-color: @dangerDeep@; }
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
    color: @primary@;
    font-weight: 600;
    padding: 4px 2px;
}
QPushButton#linkButton:hover   { color: @secondary@; }
QPushButton#linkButton:pressed { color: @primaryDeep@; }

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

QToolButton#sidebarButton {
    background: transparent;
    border: none;
    border-radius: 11px;
    color: @textMuted@;
    font-size: 13px;
    font-weight: 600;
    padding: 10px 12px;
    text-align: left;
}
QToolButton#sidebarButton:hover   { background-color: @hover@; color: @text@; }
QToolButton#sidebarButton:checked { background-color: @railActive@; color: @primary@; }

#sidebarIndicator {
    background-color: @primary@;
    border-radius: 2px;
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
    selection-color: @textOnAccent@;
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
QSpinBox::up-button, QDoubleSpinBox::up-button, QDateEdit::up-button,
QTimeEdit::up-button, QDateTimeEdit::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
    width: 20px;
    margin: 3px 5px 0px 0px;
    border: none;
    background: transparent;
}
QSpinBox::down-button, QDoubleSpinBox::down-button, QDateEdit::down-button,
QTimeEdit::down-button, QDateTimeEdit::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
    width: 20px;
    margin: 0px 5px 3px 0px;
    border: none;
    background: transparent;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow, QDateEdit::up-arrow,
QTimeEdit::up-arrow, QDateTimeEdit::up-arrow {
    image: url(@glyphs@/chevron-up.svg);
    width: 12px;
    height: 12px;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow, QDateEdit::down-arrow,
QTimeEdit::down-arrow, QDateTimeEdit::down-arrow {
    image: url(@glyphs@/chevron-down.svg);
    width: 12px;
    height: 12px;
}

QCalendarWidget QWidget { background-color: @card@; }
QCalendarWidget QAbstractItemView {
    background-color: @card@;
    color: @text@;
    selection-background-color: @primary@;
    selection-color: #FFFFFF;
    outline: 0;
}
QCalendarWidget QToolButton { color: @text@; font-weight: 600; }

/* --- 9. Tables --------------------------------------------------------------------- */
QTableView, QTableWidget, QTreeView {
    background-color: @card@;
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
    color: @textMuted@;
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
QTableCornerButton::section {
    background-color: @headerTint@;
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
    padding: 11px 14px;
    border-radius: 10px;
}

/* --- 11. Scroll bars --------------------------------------------------------------- */
QScrollBar:vertical {
    background: transparent;
    width: 11px;
    margin: 4px 2px 4px 2px;
}
QScrollBar::handle:vertical {
    background-color: @handle@;
    border-radius: 4px;
    min-height: 36px;
}
QScrollBar::handle:vertical:hover { background-color: @handleHot@; }

QScrollBar:horizontal {
    background: transparent;
    height: 11px;
    margin: 2px 4px 2px 4px;
}
QScrollBar::handle:horizontal {
    background-color: @handle@;
    border-radius: 4px;
    min-width: 36px;
}
QScrollBar::handle:horizontal:hover { background-color: @handleHot@; }

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
    color: @primary@;
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

QProgressBar {
    background-color: @hover@;
    border: none;
    border-radius: 5px;
    min-height: 8px;
    max-height: 8px;
    text-align: center;
    color: @textMuted@;
}
QProgressBar::chunk {
    background-color: @primary@;
    border-radius: 5px;
}

#splashProgress {
    background-color: @accentSoft@;
}
#splashProgress::chunk {
    background-color: @primary@;
    border-radius: 5px;
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

/* --- 17. Receipt preview ------------------------------------------------------------ */
/* A printed receipt is column-aligned monospace text; rendering it in the UI font would
   misalign every price. */
#receiptView {
    font-family: "SF Mono", "Menlo", "Consolas", "DejaVu Sans Mono", monospace;
    font-size: 12px;
    background-color: @card@;
    border: 1px solid @border@;
    border-radius: 12px;
    padding: 14px 16px;
}

#pageStack {
    background: transparent;
}

/* --- 18. Charts -------------------------------------------------------------------- */
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
        {QStringLiteral("@primary@"), hex(p.primary)},
        {QStringLiteral("@primaryDeep@"), hex(primaryDeep)},
        {QStringLiteral("@secondary@"), hex(p.secondary)},
        {QStringLiteral("@accent@"), hex(p.accent)},
        {QStringLiteral("@accentSoft@"), rgba(p.accent, light ? 0.34 : 0.22)},
        {QStringLiteral("@background@"), hex(p.background)},
        {QStringLiteral("@card@"), hex(p.card)},
        {QStringLiteral("@border@"), hex(p.border)},
        {QStringLiteral("@success@"), hex(p.success)},
        {QStringLiteral("@successWash@"), rgba(p.success, 0.12)},
        {QStringLiteral("@successLine@"), rgba(p.success, 0.45)},
        {QStringLiteral("@danger@"), hex(p.danger)},
        {QStringLiteral("@dangerDeep@"), hex(dangerDeep)},
        {QStringLiteral("@dangerWash@"), rgba(p.danger, 0.12)},
        {QStringLiteral("@dangerLine@"), rgba(p.danger, 0.45)},
        {QStringLiteral("@text@"), hex(p.text)},
        {QStringLiteral("@textOnAccent@"), light ? hex(p.text) : hex(kLight.text)},
        {QStringLiteral("@textMuted@"), hex(p.textMuted)},
        {QStringLiteral("@hover@"), hex(p.hover)},
        {QStringLiteral("@headerTint@"), hex(headerTint)},
        {QStringLiteral("@zebra@"), hex(zebra)},
        {QStringLiteral("@rail@"), hex(railTint)},
        {QStringLiteral("@railActive@"), hex(railActive)},
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

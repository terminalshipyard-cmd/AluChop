#pragma once

/**
 * @file ThemeManager.hpp
 * @brief Sage-Green design system: palette data + runtime Qt Style Sheet generation.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The whole application is themed from ONE place. A ::aluchop::gui::Palette is pure data;
 * ThemeManager turns the active palette into a complete application-wide QSS string at
 * runtime, applies it to the QApplication and announces the change with
 * ThemeManager::themeChanged. Because the stylesheet is *generated* rather than loaded from
 * a resource file, Light and Dark switch live with no restart (SPEC §1).
 *
 * @section qss_contract Generated-QSS contract (binding for every widget in this layer)
 * The generated sheet styles Qt's own classes (QPushButton, QLineEdit, QTableWidget,
 * QComboBox, QHeaderView, QScrollBar, QToolTip, QMenu, QDialog …) plus the following
 * @c objectName selectors. Widgets set these names on themselves so that no widget ever
 * hard-codes a colour:
 *
 * | objectName            | Applied to                                             |
 * |-----------------------|--------------------------------------------------------|
 * | @c sidebar            | Sidebar navigation rail                                |
 * | @c sidebarButton      | Each Sidebar QToolButton (checked = active)            |
 * | @c brandLabel         | "AluChop" wordmark in the sidebar / splash             |
 * | @c pageTitle          | Big page heading inside every Page                     |
 * | @c sectionTitle       | Section heading inside a card / panel                  |
 * | @c mutedLabel         | Secondary text (Palette::textMuted)                    |
 * | @c card               | Standard opaque card surface (Palette::card)           |
 * | @c glassPanel         | Glassmorphic translucent panel                         |
 * | @c statCard           | StatCard frame                                         |
 * | @c statCardTitle / @c statCardValue / @c statCardDelta | StatCard internals      |
 * | @c searchBar          | SearchBar line edit                                    |
 * | @c primaryButton      | Filled call-to-action button (Palette::primary)        |
 * | @c ghostButton        | Outlined/secondary button                              |
 * | @c dangerButton       | Destructive action button (Palette::danger)            |
 * | @c toast              | A single Toast card                                    |
 * | @c toastTitle / @c toastMessage | Toast internals                              |
 * | @c commandPalette / @c paletteInput / @c paletteResults | CommandPalette         |
 * | @c emptyState / @c emptyStateTitle / @c emptyStateHint  | EmptyState             |
 * | @c loadingOverlay / @c loadingOverlayText              | LoadingOverlay          |
 * | @c footerCredit       | MainWindow attribution footer (SPEC §10)               |
 * | @c elegantTable       | ElegantTable (borderless, zebra, tall rows)            |
 *
 * @note Qt gotcha, deliberately designed around: a stylesheet background does **not** paint
 *       on a direct @c QWidget subclass unless that subclass draws @c QStyle::PE_Widget in
 *       its @c paintEvent. Every themed surface in this layer therefore derives from
 *       @c QFrame (or sets @c setAutoFillBackground(true) with a QPalette taken from
 *       ThemeManager::palette()). Sidebar and ToastHost are @c QWidget by architectural
 *       contract and use the QPalette route.
 */

#include <QColor>
#include <QObject>
#include <QString>

class QApplication;

namespace aluchop::gui {

/// @oop-concept Structures :: a palette is pure data; two const instances define both themes
///
/// Nine semantic colours from SPEC §1 plus three derived working colours. No behaviour, no
/// invariants, therefore a struct with public members rather than a class.
struct Palette {
    QColor primary;     ///< Brand green — sidebar active state, primary buttons, chart series.
    QColor secondary;   ///< Softer green — hovers, secondary emphasis.
    QColor accent;      ///< Light sage — highlights, selection tints, chart accents.
    QColor background;  ///< Window/desktop backdrop behind cards.
    QColor card;        ///< Opaque card / panel surface.
    QColor border;      ///< 1px hairline separators and input outlines.
    QColor success;     ///< Positive deltas, "Ready"/"Served" chips, in-stock badges.
    QColor danger;      ///< Destructive actions, low-stock and expiry alerts.
    QColor text;        ///< Primary foreground text.
    QColor textMuted;   ///< Secondary/caption text (derived).
    QColor hover;       ///< Row/button hover wash (derived).
    QColor shadow;      ///< QGraphicsDropShadowEffect colour incl. alpha (derived).
};

/**
 * @brief Application-wide theme engine (Light/Dark) and QSS generator.
 *
 * Meyers singleton: the theme is genuinely process-wide state (SPEC §1 requires a live
 * switch from anywhere — Settings page, Ctrl+T shortcut, command palette), so a single
 * instance with a signal is the honest model rather than passing a theme object through
 * every constructor.
 *
 * Ownership: the instance is a function-local static; it is never parented and never
 * deleted by callers (ARCHITECTURE §9 rule 6).
 */
class ThemeManager : public QObject {
    Q_OBJECT
public:
    /// @oop-concept Enumerations :: the closed vocabulary of supported themes
    enum class Mode {
        Light,  ///< SPEC §1 Sage Green palette, verbatim hex values.
        Dark    ///< Deep desaturated green-greys derived from the same hues.
    };

    /// @oop-concept Static Members :: process-wide theme state behind a Meyers singleton
    /// @return the one and only theme manager, constructed on first use.
    static ThemeManager& instance();

    /// @oop-concept Constant Member Functions :: observers never mutate the theme
    /// @return the currently active mode.
    Mode mode() const noexcept { return m_mode; }

    /// Switches theme, regenerates the stylesheet, re-applies it to qApp and emits
    /// themeChanged(). A no-op (no signal) when @p m is already active.
    /// @param m the mode to activate.
    void setMode(Mode m);

    /// Flips Light <-> Dark. Bound to Ctrl+T in MainWindow and to the Settings page switch.
    void toggle();

    /// @oop-concept Return by Reference :: the active palette is observed in place, never copied
    /// @return const reference to kLight or kDark depending on mode().
    const Palette& palette() const noexcept;

    /// Builds the complete application stylesheet from palette() at runtime.
    /// @return a QSS string honouring the objectName contract documented in this file:
    ///         8–16 px radii, hairline borders, generous padding, no harsh gridlines.
    QString styleSheet() const;

    /// Applies styleSheet() to @p app. Called once at start-up (before the splash screen)
    /// and again from setMode().
    /// @param app the running QApplication (passed by reference — never null, never owned).
    void apply(QApplication& app);

    /// @oop-concept Constant Objects :: immutable palettes, SPEC §1 verbatim
    static const Palette kLight;  ///< #5D7A66 / #7E9B84 / #A8C3A1 / #F6F8F4 / #FFFFFF / #D7E4D2 / #5E9E66 / #D16464 / #1F2D1F
    static const Palette kDark;   ///< #7E9B84 / #5D7A66 / #A8C3A1 / #141A15 / #1C241D / #2A3529 / #6FBF77 / #E07A7A / #E4EBE2

signals:
    /// Emitted after a successful mode change and re-application of the stylesheet.
    /// Widgets that cache colours (charts, drop shadows, hand-painted badges) reconnect here.
    void themeChanged();

private:
    /// Private constructor — construction is the singleton's business only.
    explicit ThemeManager(QObject* parent = nullptr);

    Mode m_mode = Mode::Light;  ///< Default at start-up; main.cpp overrides from persisted settings.
};

} // namespace aluchop::gui

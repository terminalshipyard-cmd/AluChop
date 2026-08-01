/**
 * @file Page.cpp
 * @brief Implementation of the abstract screen base shared by the nine navigable pages.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Page carries no business logic and owns no widgets: it exists so MainWindow can hold nine
 * different screens in one `std::array<Page*, 9>` and drive them through a single interface.
 * The only behaviour that belongs to *every* screen lives here — identity for the stylesheet,
 * a transparent backdrop so the shell's surface shows through, and a repaint whenever the theme
 * changes so hand-painted children (charts, badges, glyphs) never keep stale colours.
 */

#include "aluchop/gui/Page.hpp"

#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/services/AppContext.hpp"

#include <QSizePolicy>

namespace aluchop::gui {

/// @oop-concept Pass by Reference :: the composition root is wired in, never copied — a Page
/// borrows the whole service graph through one reference that outlives every window
Page::Page(services::AppContext& ctx, QWidget* parent)
    : QWidget(parent), m_ctx(ctx) {
    // Identity for the generated stylesheet. A Page is a plain QWidget, so it deliberately paints
    // nothing itself: MainWindow's shell frame supplies the window backdrop and the page's own
    // cards float on top of it. See the QSS gotcha documented in ThemeManager.hpp.
    setObjectName(QStringLiteral("page"));
    setAttribute(Qt::WA_StyledBackground, false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Style-sheet colours re-apply themselves on a theme switch, but anything a subclass paints
    // by hand (QtCharts series, drop shadows, generated glyph pixmaps) will not repaint until it
    // is asked to. Every screen therefore gets a free repaint request out of this base class.
    //
    // @oop-concept Signals and Slots :: the connection is made with `this` as the context object,
    // so it is severed automatically when the screen is destroyed (ARCHITECTURE §9 rule 5).
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this]() { update(); });
}

} // namespace aluchop::gui

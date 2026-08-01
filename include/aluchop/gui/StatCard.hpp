#pragma once

/**
 * @file StatCard.hpp
 * @brief Animated dashboard statistic card (SPEC §3 "Animated dashboard").
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QFrame>
#include <QString>

class QLabel;

namespace aluchop::gui {

/**
 * @brief One headline number with a caption, an icon and an optional trend line.
 *
 * The DashboardPage shows four of these — Today's Sales, Pending Orders, Customer Count and
 * Low-Stock Items — and animates them in with a staggered fade + 12 px rise on every load.
 *
 * Derives from QFrame rather than QWidget so ThemeManager's QSS paints the card surface,
 * radius and hairline border directly (see the Qt gotcha in ThemeManager.hpp). The card sets
 * @c objectName("statCard") and gives its labels @c statCardTitle / @c statCardValue /
 * @c statCardDelta so the theme owns all colour, including the green/red delta.
 *
 * @par Effects
 * animateIn() installs a QGraphicsOpacityEffect and animates it with QPropertyAnimation.
 * Qt allows only **one** QGraphicsEffect per widget, so a StatCard uses an opacity effect and
 * gets its soft shadow from the QSS/parent panel instead of a second effect.
 *
 * @par Ownership
 * `new StatCard(...)` parented into the dashboard grid; Qt deletes it with the page. Labels
 * are parented to the card.
 *
 * @oop-concept Parameterised Constructor :: a card is meaningless without its title and icon,
 *              so there is no default constructor
 */
class StatCard : public QFrame {
    Q_OBJECT
public:
    /// @param title caption above the number, e.g. "Today's Sales".
    /// @param iconSvgPath path to the SVG glyph rendered in the card corner (Qt SVG).
    /// @param parent Qt parent that will own this card.
    StatCard(const QString& title, const QString& iconSvgPath, QWidget* parent = nullptr);

    /// Sets the headline value, already formatted by the caller
    /// (money via core::formatNpr — the card never formats currency itself).
    /// @param value display string, e.g. "Rs 42,150.00" or "7".
    void setValue(const QString& value);

    /// Sets the trend line under the value.
    /// @param delta text such as "+12% vs yesterday"; empty hides the line.
    /// @param positive true paints it with Palette::success, false with Palette::danger.
    void setDelta(const QString& delta, bool positive);

    /// Fades the card in from 0 to 1 opacity while it rises 12 px, after an optional delay.
    /// DashboardPage staggers the four cards by 0/80/160/240 ms.
    /// @param delayMs delay before the animation starts, in milliseconds.
    ///
    /// @oop-concept Default Arguments :: the un-staggered case needs no argument
    void animateIn(int delayMs = 0);

private:
    QLabel* m_title = nullptr;  ///< Caption label (objectName "statCardTitle").
    QLabel* m_value = nullptr;  ///< Headline number (objectName "statCardValue").
    QLabel* m_delta = nullptr;  ///< Trend line (objectName "statCardDelta"), hidden when empty.
    QLabel* m_icon = nullptr;   ///< SVG glyph; kept so it can be re-tinted on themeChanged().
};

} // namespace aluchop::gui

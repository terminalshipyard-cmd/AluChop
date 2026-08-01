#pragma once

/**
 * @file Sidebar.hpp
 * @brief Minimal icon navigation rail down the left edge of MainWindow.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QString>
#include <QWidget>

#include <vector>

class QToolButton;

namespace aluchop::gui {

/**
 * @brief Vertical navigation rail: one checkable icon+label button per page.
 *
 * Deliberately dumb — the sidebar knows nothing about services or page contents. It emits
 * navigate() with the index of the entry the user picked; MainWindow decides what that index
 * means. Entries are added in page order, so entry index == QStackedWidget index == the
 * Ctrl+1..9 shortcut number.
 *
 * @par Styling
 * The rail sets @c objectName("sidebar") and each button @c objectName("sidebarButton") so
 * ThemeManager's generated QSS owns every colour. Because a plain QWidget does not paint a
 * QSS background, the rail additionally calls @c setAutoFillBackground(true) and takes its
 * backdrop colour from ThemeManager::palette() on every themeChanged() (see ThemeManager.hpp).
 *
 * @par Ownership
 * Buttons are `new QToolButton(this)` — parented, therefore deleted by Qt with the Sidebar.
 * @c m_buttons only observes them; it never deletes.
 *
 * @oop-concept Pointers :: a vector of raw, Qt-parent-owned widget pointers is the idiomatic
 *              Qt collection — smart pointers would double-delete
 * @oop-concept STL vector :: the entry list grows at runtime, so std::vector, not a C array
 */
class Sidebar : public QWidget {
    Q_OBJECT
public:
    /// @param parent owning widget (MainWindow's central layout).
    explicit Sidebar(QWidget* parent = nullptr);

    /// Appends one navigation entry. Call order defines the page index.
    /// @param iconSvgPath resource/file path of the SVG icon (rendered via Qt SVG).
    /// @param label short caption shown under/next to the icon.
    void addEntry(const QString& iconSvgPath, const QString& label);

    /// Marks entry @p index as the current page (checks it, unchecks the rest).
    /// Out-of-range indices are ignored so callers need no bounds test.
    /// @param index zero-based entry index.
    void setActive(int index);

    /// @oop-concept Constant Member Functions :: observers are const
    /// @return number of entries added so far.
    int entryCount() const noexcept { return static_cast<int>(m_buttons.size()); }

    /// @return index of the currently active entry.
    int activeIndex() const noexcept { return m_active; }

signals:
    /// User asked to switch screens.
    /// @param index zero-based index of the clicked entry.
    void navigate(int index);

private:
    std::vector<QToolButton*> m_buttons;  ///< Non-owning; Qt parent owns the buttons.
    int m_active = 0;                     ///< Index of the checked entry.
};

} // namespace aluchop::gui

#pragma once

/**
 * @file Widgets.hpp
 * @brief Small reusable presentation building blocks shared by all nine screens.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * These are the pieces the nine Page subclasses assemble their layouts from: a glassmorphic
 * panel, a de-cluttered table, a search field, a titled chart container and an empty-state
 * placeholder. They carry the SPEC §1 visual language (rounded corners, soft shadow,
 * generous row height, no harsh gridlines) so that no screen re-invents it.
 *
 * @par Deliberately header-only and meta-object-free
 * None of these classes declares its own signals, slots or Q_PROPERTY — they reuse the ones
 * their Qt bases already provide (e.g. SearchBar reuses QLineEdit::textChanged). They
 * therefore need **no** @c Q_OBJECT, no moc pass and no translation unit: the kit compiles
 * into whichever screen includes it and can never produce an unresolved symbol.
 * Styling is driven entirely by @c objectName selectors (see the QSS contract in
 * ThemeManager.hpp), which works without a per-class meta-object.
 *
 * @par Ownership
 * Every widget here is created with @c new and given a Qt parent (directly or by being added
 * to a layout); Qt destroys them with their parent. Never wrap them in a smart pointer
 * (ARCHITECTURE §9 rule 1).
 *
 * @par One-effect rule
 * Qt allows a single QGraphicsEffect per widget. GlassPanel uses a QGraphicsDropShadowEffect,
 * so do not also install a QGraphicsOpacityEffect on the same panel — animate a child (or the
 * panel's geometry) instead.
 */

#include <QAbstractItemView>
#include <QColor>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>

#include "aluchop/gui/ThemeManager.hpp"

namespace aluchop::gui {

/**
 * @brief Translucent, softly shadowed surface — the glassmorphism of SPEC §1.
 *
 * The "glass" is a semi-transparent card colour (taken from the live Palette) over the tinted
 * window backdrop plus a hairline highlight border and a wide, low-opacity drop shadow. It is
 * a QFrame, so the stylesheet paints it without a custom paintEvent.
 *
 * @oop-concept Objects as Members :: the panel owns its QGraphicsDropShadowEffect object and
 *              hands it to Qt, which is exactly how Qt composition is meant to look
 */
class GlassPanel : public QFrame {
public:
    /// @param parent owning widget.
    /// @param blurRadius shadow blur in pixels — generous by design (SPEC §1 "soft shadows").
    ///
    /// @oop-concept Default Arguments :: the house style is the default; callers may tune it
    explicit GlassPanel(QWidget* parent = nullptr, int blurRadius = 28) : QFrame(parent) {
        setObjectName(QStringLiteral("glassPanel"));
        setFrameShape(QFrame::NoFrame);
        applyShadow(blurRadius, 8, 0.18);
    }

    /// Replaces the drop shadow. Colour comes from the active Palette::shadow so Dark mode
    /// gets its own, heavier shadow.
    /// @param blurRadius blur in pixels.
    /// @param yOffset vertical offset in pixels.
    /// @param opacity 0..1 multiplier applied to Palette::shadow's alpha.
    void applyShadow(int blurRadius, int yOffset, qreal opacity) {
        auto* shadow = new QGraphicsDropShadowEffect(this);
        QColor c = ThemeManager::instance().palette().shadow;
        c.setAlphaF(opacity);
        shadow->setColor(c);
        shadow->setBlurRadius(blurRadius);
        shadow->setOffset(0, yOffset);
        setGraphicsEffect(shadow);  // Qt takes ownership; any previous effect is deleted
    }
};

/**
 * @brief QTableWidget preconfigured to the AluChop table style.
 *
 * No gridlines, zebra striping, whole-row selection, 44 px rows, stretched last column,
 * hidden row numbers, read-only cells and click-to-sort headers — SPEC §1's "elegant tables,
 * no harsh gridlines, generous row height" applied once instead of in nine screens.
 *
 * Because it *is* a QTableWidget, every `QTableWidget*` member declared across the page
 * headers may point at an ElegantTable without any signature change.
 *
 * @oop-concept Single Inheritance :: pure specialisation — behaviour is inherited wholesale,
 *              only presentation defaults are overridden
 */
class ElegantTable : public QTableWidget {
public:
    /// @param parent owning widget.
    explicit ElegantTable(QWidget* parent = nullptr) : QTableWidget(parent) { applyHouseStyle(); }

    /// @param headers column captions; column count is taken from the list size.
    /// @param parent owning widget.
    ///
    /// @oop-concept Constructor Overloading :: build empty, or build already labelled
    explicit ElegantTable(const QStringList& headers, QWidget* parent = nullptr)
        : QTableWidget(0, headers.size(), parent) {
        applyHouseStyle();
        setHorizontalHeaderLabels(headers);
    }

    /// Clears every row but keeps the columns and their labels — the shape a refresh() needs.
    void clearRows() { setRowCount(0); }

private:
    /// Applies the shared look. Called from every constructor.
    void applyHouseStyle() {
        setObjectName(QStringLiteral("elegantTable"));
        setShowGrid(false);
        setAlternatingRowColors(true);
        setSelectionBehavior(QAbstractItemView::SelectRows);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        setSortingEnabled(true);
        setWordWrap(false);
        setFrameShape(QFrame::NoFrame);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        verticalHeader()->setVisible(false);
        verticalHeader()->setDefaultSectionSize(44);
        horizontalHeader()->setHighlightSections(false);
        horizontalHeader()->setStretchLastSection(true);
        horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
};

/**
 * @brief Rounded search field with a leading glyph, placeholder and a clear button.
 *
 * A QLineEdit subclass, so the `QLineEdit* m_search` members declared in MenuPage,
 * CustomersPage and friends can point at one directly, and every existing signal
 * (@c textChanged, @c returnPressed) keeps working — which is exactly why this class needs no
 * meta-object of its own.
 */
class SearchBar : public QLineEdit {
public:
    /// @param placeholder hint text, e.g. "Search menu, category or description…".
    /// @param parent owning widget.
    explicit SearchBar(const QString& placeholder = QStringLiteral("Search…"),
                       QWidget* parent = nullptr)
        : QLineEdit(parent) {
        setObjectName(QStringLiteral("searchBar"));
        setPlaceholderText(placeholder);
        setClearButtonEnabled(true);
        setMinimumHeight(38);
        setTextMargins(6, 0, 6, 0);
    }
};

/**
 * @brief Titled card that frames a QtCharts view — the dashboard/report chart container.
 *
 * Keeps chart plumbing out of the screens: they build a QChart, hand it over, and the card
 * takes care of the surface, the heading and the antialiasing.
 *
 * @par Ownership
 * QChartView takes ownership of the QChart passed to setChart(); the previous chart is
 * deleted. The view itself is parented to this card.
 */
class ChartCard : public QFrame {
public:
    /// @param title section heading above the plot, e.g. "Revenue — last 7 days".
    /// @param parent owning widget.
    explicit ChartCard(const QString& title, QWidget* parent = nullptr) : QFrame(parent) {
        setObjectName(QStringLiteral("card"));
        setFrameShape(QFrame::NoFrame);

        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(18, 16, 18, 16);
        column->setSpacing(10);

        m_title = new QLabel(title, this);
        m_title->setObjectName(QStringLiteral("sectionTitle"));

        m_view = new QChartView(this);
        m_view->setRenderHint(QPainter::Antialiasing, true);
        m_view->setFrameShape(QFrame::NoFrame);
        m_view->setMinimumHeight(220);

        column->addWidget(m_title);
        column->addWidget(m_view, 1);
    }

    /// Installs @p chart, transferring ownership to the internal view.
    /// @param chart a fully configured QChart; may be null to clear.
    void setChart(QChart* chart) { m_view->setChart(chart); }

    /// @return the embedded view, so a screen can keep the `QChartView*` member its header
    ///         declares while still getting the framed presentation.
    QChartView* view() const noexcept { return m_view; }

    /// Updates the heading (report screens retitle the same card per report kind).
    void setTitle(const QString& title) { m_title->setText(title); }

private:
    QLabel* m_title = nullptr;    ///< Qt-parent-owned heading.
    QChartView* m_view = nullptr; ///< Qt-parent-owned plot surface.
};

/**
 * @brief Centred placeholder shown instead of an empty table or list.
 *
 * "No orders yet", "Nothing low on stock — nice", "No reservations for this day". Keeps
 * screens from looking broken when the database is simply empty, which matters on first run
 * right after seeding.
 */
class EmptyState : public QFrame {
public:
    /// @param title one-line explanation of the emptiness.
    /// @param hint optional secondary line telling the user what to do next.
    /// @param parent owning widget.
    explicit EmptyState(const QString& title, const QString& hint = QString(),
                        QWidget* parent = nullptr)
        : QFrame(parent) {
        setObjectName(QStringLiteral("emptyState"));
        setFrameShape(QFrame::NoFrame);

        auto* column = new QVBoxLayout(this);
        column->setAlignment(Qt::AlignCenter);
        column->setSpacing(6);

        m_title = new QLabel(title, this);
        m_title->setObjectName(QStringLiteral("emptyStateTitle"));
        m_title->setAlignment(Qt::AlignCenter);

        m_hint = new QLabel(hint, this);
        m_hint->setObjectName(QStringLiteral("emptyStateHint"));
        m_hint->setAlignment(Qt::AlignCenter);
        m_hint->setWordWrap(true);
        m_hint->setVisible(!hint.isEmpty());

        m_action = new QPushButton(this);
        m_action->setObjectName(QStringLiteral("ghostButton"));
        m_action->setVisible(false);

        column->addWidget(m_title);
        column->addWidget(m_hint);
        column->addWidget(m_action, 0, Qt::AlignHCenter);
    }

    /// Replaces both text lines at once (the hint hides itself when empty).
    void setText(const QString& title, const QString& hint = QString()) {
        m_title->setText(title);
        m_hint->setText(hint);
        m_hint->setVisible(!hint.isEmpty());
    }

    /// Reveals the call-to-action button, e.g. "Add the first ingredient".
    /// @param label button caption.
    /// @return the button, so the caller can connect its clicked() signal.
    QPushButton* enableAction(const QString& label) {
        m_action->setText(label);
        m_action->setVisible(true);
        return m_action;
    }

private:
    QLabel* m_title = nullptr;      ///< Qt-parent-owned headline.
    QLabel* m_hint = nullptr;       ///< Qt-parent-owned secondary line.
    QPushButton* m_action = nullptr;///< Qt-parent-owned optional call to action.
};

} // namespace aluchop::gui

#pragma once

/**
 * @file ChartKit.hpp
 * @brief The single place that decides how an AluChop bar chart looks.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Two screens draw bar charts — gui::DashboardPage (the seven-day revenue series) and
 * gui::ReportsPage (whatever column the selected report turns out to be able to plot). They used
 * to configure their axes independently, and the two drifted: the dashboard scaled its value axis
 * straight off the data and printed ticks like `0 · 2677 · 5354 · 8031 · 10709`, while Reports
 * rounded to `0 · 5000 · 10000 · 15000`. Two tick algorithms in one product is exactly the kind of
 * detail that makes software look unfinished, so the rules live here and nowhere else:
 *
 *  - ticks are always **nice round numbers** (QValueAxis::applyNiceNumbers),
 *  - the plot always keeps ~18 % headroom above the tallest bar so a bar never touches the top,
 *  - a series with nothing in it always falls back to the same 0–1 axis, so the two screens'
 *    empty states are identical rather than one showing `0–100` and the other `0–1`,
 *  - every colour is read from the live gui::Palette, so Light and Dark both work.
 *
 * @par Deliberately header-only
 * These are four short inline functions over Qt types with no state of their own — the same
 * reasoning that makes gui::Widgets.hpp header-only (no Q_OBJECT, no meta-object, no translation
 * unit, so it can never produce an unresolved symbol).
 *
 * @oop-concept Namespaces :: `chartkit` is a named, importable vocabulary of chart rules rather
 *              than a class nobody would ever instantiate
 * @oop-concept Default Arguments :: the house chart is the default; a caller tunes only what its
 *              own screen genuinely needs (tilted date labels, a longer entrance animation)
 */

#include <QMargins>
#include <QString>
#include <QStringList>

#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QChart>
#include <QtCharts/QLegend>
#include <QtCharts/QValueAxis>

#include <algorithm>

#include "aluchop/gui/ThemeManager.hpp"

namespace aluchop::gui::chartkit {

/// Headroom left above the tallest bar (and below the deepest) before the axis is rounded off.
///
/// Small on purpose: applyNiceNumbers() rounds the range *outward* to the next round figure and so
/// supplies most of the headroom itself. A generous multiplier on top of that compounds — a
/// Rs 27,500 day asked for 32,450, which rounded up to a 40,000 axis and left the tallest bar
/// sitting at two-thirds height in a mostly empty plot. This is just enough that a bar whose value
/// is already a round number does not touch the top gridline.
constexpr double kHeadroom = 1.06;
constexpr double kNegativeHeadroom = 1.06;

/**
 * @brief Is there anything worth drawing?
 * @param lowest smallest value in the series (0 when the series never goes negative).
 * @param highest largest value in the series.
 * @return false when every bar would have zero height.
 *
 * Both screens ask this same question before they decide between a plot and an empty state, so
 * "nothing to show" means the same thing on the dashboard as it does on Reports.
 */
inline bool hasPlottableRange(double lowest, double highest) {
    return lowest != 0.0 || highest != 0.0;
}

/**
 * @brief A bare chart surface: no legend, no background, ready for one series.
 * @param margins padding reserved for the axis labels; the default suits a vertical bar chart.
 * @param animationMs entrance animation length in milliseconds; 0 disables the animation.
 */
inline QChart* newChart(const QMargins& margins = QMargins(4, 8, 10, 4), int animationMs = 420) {
    auto* chart = new QChart();
    chart->legend()->hide();
    chart->setBackgroundVisible(false);
    chart->setPlotAreaBackgroundVisible(false);
    // Room on every side for the axis labels and the baseline: with a zero bottom margin the
    // category axis of a vertical chart is the first thing a short card slices off.
    chart->setMargins(margins);
    if (animationMs > 0) {
        chart->setAnimationOptions(QChart::SeriesAnimations);
        chart->setAnimationDuration(animationMs);
    }
    return chart;
}

/**
 * @brief The category axis in house style.
 * @param categories one label per bar, already shortened by the caller if it had to be.
 * @param pal the live palette.
 * @param labelAngle rotation in degrees; -45 is what keeps a dozen dates from colliding.
 */
inline QBarCategoryAxis* newCategoryAxis(const QStringList& categories, const Palette& pal,
                                         int labelAngle = 0) {
    auto* axis = new QBarCategoryAxis();
    axis->append(categories);
    axis->setLabelsColor(pal.textMuted);
    axis->setLineVisible(false);
    axis->setGridLineVisible(false);
    // QtCharts otherwise truncates a long category to a bare ellipsis. Labels are shortened
    // deliberately by the caller, so they are drawn in full from here.
    axis->setTruncateLabels(false);
    if (labelAngle != 0) axis->setLabelsAngle(labelAngle);
    return axis;
}

/**
 * @brief The value axis in house style, with **nice round ticks** — the rule this file exists for.
 * @param lowest smallest value in the series (pass 0.0 for a series that never goes negative).
 * @param highest largest value in the series.
 * @param pal the live palette.
 *
 * The raw range is `0 … highest × 1.18`, which on real data is an arbitrary number such as
 * 10 709. QValueAxis::applyNiceNumbers() then walks that up to the nearest figure a person would
 * have chosen, so the ticks read 0 · 3 000 · 6 000 · 9 000 · 12 000 instead of 0 · 2 677 · 5 354 …
 * An empty series is given the same 0–1 axis on every screen.
 */
inline QValueAxis* newValueAxis(double lowest, double highest, const Palette& pal) {
    auto* axis = new QValueAxis();
    const double low = std::min(0.0, lowest);
    const double high = std::max(0.0, highest);

    // Sub-ten figures (a stock count in kilos, a rating) need their decimals; rupees never do.
    axis->setLabelFormat(high > 0.0 && high < 10.0 ? QStringLiteral("%.2f")
                                                   : QStringLiteral("%.0f"));
    axis->setTickCount(5);
    if (hasPlottableRange(low, high)) {
        axis->setRange(low < 0.0 ? low * kNegativeHeadroom : 0.0,
                       high > 0.0 ? high * kHeadroom : 1.0);
        axis->applyNiceNumbers();
    } else {
        // Identical on both screens: an empty chart is never left with a range invented from
        // whatever the local code happened to guess.
        axis->setRange(0.0, 1.0);
    }

    axis->setLabelsColor(pal.textMuted);
    axis->setLineVisible(false);
    axis->setGridLineColor(pal.border);
    axis->setMinorGridLineVisible(false);
    return axis;
}

} // namespace aluchop::gui::chartkit

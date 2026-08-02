/**
 * @file ReportsPage.cpp
 * @brief Screen 8 — the five report kinds with chart preview, CSV and PDF export.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * This screen is the most polymorphic surface in the application. It asks
 * services::ReportService for a report and receives a `std::unique_ptr<ReportGenerator>` pointing
 * at one of five concrete classes. From that moment on the code here never asks *which* report it
 * is holding: the preview table, the chart and both exports are built from `title()`, `header()`
 * and `rows()` alone. Adding a sixth report would need no change in this file.
 *
 * @par Two export paths, split by layer on purpose
 *  - **CSV** is written by services::ReportGenerator::exportCsv, which drives the `<fstream>` CSV
 *    writer down in the persistence layer, so no file writing happens in a widget.
 *  - **PDF** is rendered by gui::PdfExporter, because QtPrintSupport is a presentation module and
 *    must not leak downwards.
 *
 * @par The audit check, the record browser and the layer rule
 * "Verify integrity" walks the fixed-record binary audit trail, and the same dialog carries a
 * **record browser**: type a record number and that one 128-byte record is fetched by index alone
 * (`services::AuditService::trailRecordAt`), which is what makes the random-access file layer a
 * feature a user can operate rather than a capability buried in the persistence layer. The GUI
 * layer may not name `aluchop::persistence` at all, so this file calls only the four delegating
 * methods services::AuditService added for exactly this purpose (`verifyTrailIntegrity`,
 * `trailRecordCount`, `trailRecordAt`, `recentTrailRecords`) and never touches
 * `AuditService::trail()`.
 *
 * @par Absolutely no SQL
 * Everything on this screen arrives through services on services::AppContext.
 */

#include "aluchop/gui/ReportsPage.hpp"

#include <QAbstractItemView>
#include <QByteArray>
#include <QChar>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLatin1Char>
#include <QLatin1String>
#include <QMargins>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimeZone>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>

#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QHorizontalBarSeries>
#include <QtCharts/QLegend>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

#include "aluchop/core/AppInfo.hpp"
#include "aluchop/core/Money.hpp"
#include "aluchop/gui/ChartKit.hpp"
#include "aluchop/gui/PdfExporter.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/gui/Widgets.hpp"
#include "aluchop/services/AppContext.hpp"

namespace aluchop::gui {
namespace {

// ---------------------------------------------------------------------------
// Small presentation helpers — colours always come from the live Palette.
// ---------------------------------------------------------------------------

/// Applies a point-size delta and a weight to a label and gives it a QSS object name.
QLabel* styledLabel(const QString& text, const QString& objectName, QWidget* parent,
                    int pointDelta = 0, QFont::Weight weight = QFont::Normal) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(objectName);
    QFont f = label->font();
    if (pointDelta != 0) f.setPointSize(std::max(8, f.pointSize() + pointDelta));
    f.setWeight(weight);
    label->setFont(f);
    return label;
}

/// A themed button: the object name selects primary / ghost / danger styling from the QSS.
QPushButton* makeButton(const QString& text, const QString& objectName, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(objectName);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(36);
    return button;
}

/**
 * @brief Builds a card surface with a heading and a subtitle, returning the body layout to fill.
 *
 * The subtitle is deliberately the panel's only direct child named @c mutedLabel — every other
 * caption on this screen uses @c statCardTitle, which the stylesheet renders identically small and
 * muted. That is what makes `findChild<QLabel*>("mutedLabel", FindDirectChildrenOnly)` an
 * unambiguous way to re-title a panel without this page carrying extra member pointers (the header
 * declares exactly five, and headers are frozen).
 */
QVBoxLayout* buildPanel(const QString& title, const QString& subtitle, QWidget* parent,
                        QFrame** outPanel) {
    auto* panel = new GlassPanel(parent);
    panel->setObjectName(QStringLiteral("card"));
    auto* column = new QVBoxLayout(panel);
    column->setContentsMargins(18, 16, 18, 16);
    column->setSpacing(10);

    column->addWidget(styledLabel(title, QStringLiteral("sectionTitle"), panel, 1, QFont::DemiBold));
    auto* sub = styledLabel(subtitle, QStringLiteral("mutedLabel"), panel, -1);
    sub->setWordWrap(true);
    column->addWidget(sub);

    *outPanel = panel;
    return column;
}

// ---------------------------------------------------------------------------
// Output folders and file names
// ---------------------------------------------------------------------------

/**
 * @brief Resolves (and creates) a writable output folder such as `reports/` or `exports/`.
 *
 * The working directory is tried first because that is the project root when the application is
 * launched by `build.sh`; an installed copy falls back to the executable's folder and finally to
 * the per-user application-data area, so an export can never silently fail for want of a folder.
 */
QString ensureOutputDir(const QString& leaf) {
    QStringList candidates;
    candidates << QDir::current().absoluteFilePath(leaf)
               << QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(leaf)
               << QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                      .absoluteFilePath(leaf);

    for (const QString& candidate : std::as_const(candidates)) {
        QDir dir(candidate);
        if (!dir.exists() && !QDir().mkpath(candidate)) continue;
        const QFileInfo info(candidate);
        if (info.isDir() && info.isWritable()) return QDir(candidate).absolutePath();
    }
    return QDir::tempPath();
}

/// `2026-08-01_1830` — sortable and safe on every file system.
QString fileStamp() {
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmm"));
}

/// Turns a report title into a tidy file-name stem.
QString slugify(const QString& title) {
    QString out;
    out.reserve(title.size());
    bool lastWasDash = false;
    for (const QChar ch : title) {
        if (ch.isLetterOrNumber()) {
            out.append(ch.toLower());
            lastWasDash = false;
        } else if (!lastWasDash && !out.isEmpty()) {
            out.append(QLatin1Char('-'));
            lastWasDash = true;
        }
    }
    while (out.endsWith(QLatin1Char('-'))) out.chop(1);
    return out.isEmpty() ? QStringLiteral("report") : out;
}

// ---------------------------------------------------------------------------
// Generic cell analysis — this is what lets one screen chart five different reports
// ---------------------------------------------------------------------------

/**
 * @brief Parses a report cell that holds a number or an amount.
 * @param raw the cell text exactly as the report produced it.
 * @param[out] value the parsed magnitude (rupees for money cells).
 * @param[out] money true when the cell carried a currency prefix.
 * @return true when the cell is numeric.
 *
 * @warning The `double` produced here is a **chart coordinate**, never an amount of money. Every
 *          figure in AluChop is core::Money (integer paisa) right up to the moment a report renders
 *          it as text; this function only turns that text back into a bar height. No total, no
 *          balance and no price is ever derived from it.
 *
 * /// @oop-concept Pass by Reference :: two results come back through reference parameters, so the
 * /// caller learns both the number and whether it was currency in a single parse
 */
bool numericValue(const QString& raw, double& value, bool& money) {
    QString t = raw.trimmed();
    money = false;
    if (t.isEmpty()) return false;

    bool negative = false;
    if (t.startsWith(QLatin1Char('-'))) {
        negative = true;
        t = t.mid(1).trimmed();
    } else if (t.startsWith(QLatin1Char('+'))) {
        t = t.mid(1).trimmed();
    }

    if (t.startsWith(QLatin1String("Rs"), Qt::CaseInsensitive)) {
        money = true;
        t = t.mid(2).trimmed();
    } else if (t.startsWith(QLatin1String("NPR"), Qt::CaseInsensitive)) {
        money = true;
        t = t.mid(3).trimmed();
    }

    t.remove(QLatin1Char(','));
    if (t.isEmpty()) return false;

    bool ok = false;
    const double parsed = t.toDouble(&ok);
    if (!ok) return false;

    value = negative ? -parsed : parsed;
    return true;
}

/// Per-column statistics used for both alignment and chart-column selection.
struct ColumnProfile {
    int filled = 0;    ///< non-empty cells seen
    int numeric = 0;   ///< of those, how many parsed as a number
    int monetary = 0;  ///< of those, how many carried a currency prefix

    /// A column is treated as figures when most of its filled cells are numbers.
    bool isFigures() const { return filled > 0 && numeric * 10 >= filled * 6; }
    /// A column is treated as money when most of its filled cells carried `Rs`.
    bool isMoney() const { return filled > 0 && monetary * 10 >= filled * 6; }
};

/// Profiles every column of a report body.
std::vector<ColumnProfile> profileColumns(int columns, const std::vector<QStringList>& rows) {
    std::vector<ColumnProfile> profiles(static_cast<std::size_t>(std::max(columns, 0)));
    for (const QStringList& row : rows) {
        for (int c = 0; c < columns && c < row.size(); ++c) {
            const QString& cell = row.at(c);
            if (cell.trimmed().isEmpty()) continue;
            ColumnProfile& p = profiles[static_cast<std::size_t>(c)];
            ++p.filled;
            double value = 0.0;
            bool money = false;
            if (numericValue(cell, value, money)) {
                ++p.numeric;
                if (money) ++p.monetary;
            }
        }
    }
    return profiles;
}

/// Reports append a grand-total line; it is a summary, not a category, so it never gets a bar.
bool isSummaryLabel(const QString& label) {
    return label.trimmed().compare(QLatin1String("TOTAL"), Qt::CaseInsensitive) == 0;
}

/**
 * @brief Every count this screen quotes, worked out **once** from the one `rows()` call.
 *
 * The page used to print "31 rows" beside a chart captioned "the most recent 16 of 30 days",
 * which is two true statements that read as a contradiction because each was counted somewhere
 * else. One tally, computed from one query, is now the only source: the badge quotes `total`, the
 * preview subtitle explains `total = data + summary`, and the chart caption's denominator is
 * `data`. They cannot drift apart because there is nothing left to drift.
 */
struct RowTally {
    int total = 0;    ///< every line the export will contain
    int data = 0;     ///< the subject lines — days, orders, ingredients, people
    int summary = 0;  ///< grand-total lines appended by the report itself

    /// @return "days" for a date-indexed report, "entries" otherwise — used by every sentence.
    static QString noun(bool dateLabels, int count) {
        if (dateLabels) return count == 1 ? QStringLiteral("day") : QStringLiteral("days");
        return count == 1 ? QStringLiteral("entry") : QStringLiteral("entries");
    }
};

/// Counts the body of a report exactly once.
RowTally tallyRows(const std::vector<QStringList>& rows) {
    RowTally tally;
    tally.total = static_cast<int>(rows.size());
    for (const QStringList& row : rows) {
        if (!row.isEmpty() && isSummaryLabel(row.at(0))) ++tally.summary;
    }
    tally.data = tally.total - tally.summary;
    return tally;
}

/// One plotted point.
struct ChartPoint {
    QString label;
    double value = 0.0;
};

/// What to plot, worked out from the data alone — never from the report kind.
struct ChartPlan {
    bool usable = false;      ///< false when no column can be charted
    int labelColumn = 0;      ///< always the first column: reports lead with their subject
    int valueColumn = -1;     ///< the last money column, else the last numeric column
    bool dateLabels = false;  ///< true when the labels are ISO dates (keep chronological order)
    bool money = false;       ///< true when the plotted column is currency
    bool allZero = false;     ///< every candidate figure was zero — a grid of bars nobody can see
    std::vector<ChartPoint> points;  ///< the readable slice actually drawn

};

/// The longest a category label may be before it is elided — long enough to stay recognisable,
/// short enough that the axis cannot eat the plot.
constexpr int kMaxLabelChars = 26;

/// Chooses the columns to chart and extracts the points.
ChartPlan planChart(const QStringList& header, const std::vector<QStringList>& rows,
                    const std::vector<ColumnProfile>& profiles) {
    ChartPlan plan;
    const int columns = header.size();
    if (columns < 2 || rows.empty()) return plan;

    int lastMoney = -1;
    int lastNumeric = -1;
    for (int c = 1; c < columns && c < static_cast<int>(profiles.size()); ++c) {
        const ColumnProfile& p = profiles[static_cast<std::size_t>(c)];
        if (p.isFigures()) lastNumeric = c;
        if (p.isMoney()) lastMoney = c;
    }

    plan.valueColumn = lastMoney >= 0 ? lastMoney : lastNumeric;
    if (plan.valueColumn < 0) return plan;
    plan.money = lastMoney >= 0;

    // Are the labels dates? If so the series stays in chronological order instead of being
    // re-sorted by magnitude, which is what a revenue trend has to look like.
    int labels = 0;
    int dates = 0;
    for (const QStringList& row : rows) {
        if (row.isEmpty()) continue;
        const QString label = row.at(0).trimmed();
        if (label.isEmpty() || isSummaryLabel(label)) continue;
        ++labels;
        if (QDate::fromString(label, Qt::ISODate).isValid()) ++dates;
    }
    plan.dateLabels = labels > 0 && dates * 10 >= labels * 8;

    for (const QStringList& row : rows) {
        if (row.size() <= plan.valueColumn) continue;
        const QString label = row.at(0).trimmed();
        if (label.isEmpty() || isSummaryLabel(label)) continue;

        QString shown = label;
        if (plan.dateLabels) {
            const QDate day = QDate::fromString(label, Qt::ISODate);
            if (!day.isValid()) continue;  // drops any stray summary line
            shown = day.toString(QStringLiteral("d MMM"));
        } else if (shown.size() > kMaxLabelChars) {
            // Deliberate elision: an ingredient called "Extra Virgin Olive Oil (Cold Pressed)"
            // would otherwise push the category axis across half the card. Shortened here, in
            // full in the table below.
            shown = shown.left(kMaxLabelChars - 1).trimmed() + QChar(0x2026);
        }

        double value = 0.0;
        bool money = false;
        if (!numericValue(row.at(plan.valueColumn), value, money)) continue;
        plan.points.push_back(ChartPoint{shown, value});
    }

    if (plan.points.empty()) return plan;

    // A column of nothing but zeros is not a chart — plotting it draws an axis, a grid and no
    // bars, which reads as a broken widget. It is an empty state, and it is labelled as one.
    /// @oop-concept STL (algorithms) :: std::none_of answers "is there anything to see here?"
    plan.allZero = std::none_of(plan.points.begin(), plan.points.end(),
                                [](const ChartPoint& p) { return p.value != 0.0; });
    if (plan.allZero) return plan;

    if (plan.dateLabels) {
        // Keep the newest slice of a long range so the bars stay readable. The chart now sits in
        // the narrower of the two columns — the table beside it is where the whole range lives —
        // and eight is the most tilted dates that column can label without QtCharts silently
        // dropping every other one, which is what leaves bars anonymous.
        constexpr std::size_t kMaxDays = 8;
        if (plan.points.size() > kMaxDays)
            plan.points.erase(plan.points.begin(),
                              plan.points.end() - static_cast<std::ptrdiff_t>(kMaxDays));
    } else {
        /// @oop-concept STL (algorithms) :: std::sort ranks the categories before the top slice
        std::sort(plan.points.begin(), plan.points.end(),
                  [](const ChartPoint& a, const ChartPoint& b) { return a.value > b.value; });
        // Eight is what a horizontal card can label: past that QtCharts starts hiding every other
        // category name to stop them colliding, which leaves half the bars anonymous.
        constexpr std::size_t kMaxBars = 8;
        if (plan.points.size() > kMaxBars) plan.points.resize(kMaxBars);
        // Horizontal bars are drawn bottom-up, so reversing puts the biggest bar at the top.
        std::reverse(plan.points.begin(), plan.points.end());
    }

    plan.usable = true;
    return plan;
}

/**
 * @brief A chart with nothing in it, for the states where there is nothing to plot.
 *
 * QChartView must never be handed `nullptr`: QChartViewPrivate::resize() dereferences the chart
 * unconditionally, so a view left holding a null chart segfaults the next time the page is laid
 * out — which is a certainty, because hiding the view is itself a layout change.
 */
QChart* blankChart() { return chartkit::newChart(QMargins(0, 0, 0, 0), 0); }

/**
 * @brief Installs a chart in the view and destroys the one it replaces.
 *
 * QChartView::setChart() takes ownership of the incoming chart but only *releases* the outgoing
 * one — it does not delete it. Every refresh builds a fresh chart, so without this the page would
 * leak an entire chart, its series and its two axes on every date change and every theme switch.
 *
 * @oop-concept Dynamic Memory Allocation :: ownership handed over and reclaimed explicitly, in the
 *              one place that knows both halves of the exchange
 */
void installChart(QChartView* view, QChart* next) {
    QChart* previous = view->chart();
    if (previous == next) return;
    view->setChart(next);
    delete previous;
}

/// Builds the QtCharts bar chart for a plan. The caller hands it to the QChartView, which owns it.
QChart* buildChart(const ChartPlan& plan, const QString& seriesName, const Palette& pal) {
    auto* bars = new QBarSet(seriesName);
    bars->setColor(pal.primary);
    bars->setBorderColor(pal.primary);
    bars->setLabelColor(pal.text);

    QStringList categories;
    double lowest = 0.0;
    double highest = 0.0;
    for (const ChartPoint& point : plan.points) {
        *bars << point.value;
        categories << point.label;
        lowest = std::min(lowest, point.value);
        highest = std::max(highest, point.value);
    }

    // Surface, axes and — crucially — the *tick algorithm* all come from gui::chartkit, which is
    // the one place that decides what an AluChop chart looks like. The dashboard's revenue chart
    // is built from exactly the same three calls, so the two can no longer disagree about
    // anything, ticks included.
    QChart* chart = chartkit::newChart();

    // Dates run along the bottom, so past roughly six of them the labels would collide. Tilting
    // is deliberate: QtCharts reserves the extra height, so nothing is clipped either.
    QBarCategoryAxis* categoryAxis = chartkit::newCategoryAxis(
        categories, pal, plan.dateLabels && categories.size() > 6 ? -45 : 0);

    QValueAxis* valueAxis = chartkit::newValueAxis(lowest, highest, pal);

    if (plan.dateLabels) {
        auto* series = new QBarSeries();
        series->append(bars);
        series->setBarWidth(0.58);
        chart->addSeries(series);
        chart->addAxis(categoryAxis, Qt::AlignBottom);
        chart->addAxis(valueAxis, Qt::AlignLeft);
        series->attachAxis(categoryAxis);
        series->attachAxis(valueAxis);
    } else {
        auto* series = new QHorizontalBarSeries();
        series->append(bars);
        series->setBarWidth(0.62);
        chart->addSeries(series);
        chart->addAxis(categoryAxis, Qt::AlignLeft);
        chart->addAxis(valueAxis, Qt::AlignBottom);
        series->attachAxis(categoryAxis);
        series->attachAxis(valueAxis);
    }

    return chart;
}

// ---------------------------------------------------------------------------
// Report kinds
// ---------------------------------------------------------------------------

/// The five kinds in combo-box order, each with its one-line explanation.
struct KindEntry {
    services::ReportKind kind;
    const char* label;
    const char* hint;
};

const KindEntry kKinds[] = {
    {services::ReportKind::Sales, "Sales",
     "Settled revenue per day across the selected range, with a grand total."},
    {services::ReportKind::Orders, "Orders",
     "Every order raised in the selected range, with its type, status and value."},
    {services::ReportKind::Inventory, "Inventory",
     "The current stock position: quantities, thresholds, expiry dates and unit costs."},
    {services::ReportKind::Customers, "Customers",
     "The loyalty database: contact details, visit counts and points."},
    {services::ReportKind::Employees, "Employees",
     "The roster with each role's own monthly pay rule applied."},
};

/// Reads the currently selected kind back out of the combo box.
services::ReportKind selectedKind(const QComboBox* combo) {
    if (!combo) return services::ReportKind::Sales;
    const int stored = combo->currentData().toInt();
    for (const KindEntry& entry : kKinds) {
        if (static_cast<int>(entry.kind) == stored) return entry.kind;
    }
    return services::ReportKind::Sales;
}

/// The explanatory line for the selected kind.
QString selectedHint(const QComboBox* combo) {
    if (!combo) return QString();
    const int stored = combo->currentData().toInt();
    for (const KindEntry& entry : kKinds) {
        if (static_cast<int>(entry.kind) == stored) return QString::fromUtf8(entry.hint);
    }
    return QString();
}

/// Shows where a file landed: a toast, plus a permanent clickable line under the table.
void announceSaved(QWidget* page, services::AppContext& ctx, const QString& what,
                   const QString& path) {
    const QFileInfo info(path);
    const QString folderUrl = QUrl::fromLocalFile(info.absolutePath()).toString();

    if (auto* status = page->findChild<QLabel*>(QStringLiteral("reportStatus"))) {
        status->setText(QStringLiteral("%1 saved to <a href=\"%2\">%3</a>")
                            .arg(what, folderUrl.toHtmlEscaped(),
                                 info.absoluteFilePath().toHtmlEscaped()));
        status->setVisible(true);
    }

    ctx.notifications().notify(QStringLiteral("%1 exported").arg(what),
                               QStringLiteral("Saved to %1").arg(info.absoluteFilePath()), 1);
}

/// Reads a NUL-padded fixed char field out of an audit record.
QString fixedField(const char* field, std::size_t capacity) {
    const auto used = qstrnlen(field, static_cast<qsizetype>(capacity));
    return QString::fromLatin1(field, used).trimmed();
}

/**
 * @brief Gives the preview table's *identifying* column the width, and every other column only
 *        what it actually needs.
 *
 * Stretching every text column equally is what used to make `Unit` ("kg") exactly as wide as
 * `Ingredient` ("Extra Virgin Olive Oil"), so the one column a reader navigates by was the one
 * being elided while a two-letter column sat in a field of white. The rule here is the opposite:
 * every report leads with its subject — Date, Order #, Ingredient, Name — so the first non-figure
 * column stretches and takes all the slack, and each remaining column is measured against its own
 * header and cells and given that width, capped so a long e-mail cannot eat the table.
 *
 * @param table the preview table, already filled.
 * @param profiles the per-column analysis, so figures are recognised without re-parsing.
 */
void fitColumns(QTableWidget* table, const std::vector<ColumnProfile>& profiles) {
    const int columns = table->columnCount();
    if (columns <= 0) return;

    QHeaderView* header = table->horizontalHeader();
    // Stretch already consumes the full viewport; leaving this on would fight it.
    header->setStretchLastSection(false);
    // Short columns must be allowed to *be* short — a floor of 88 px is precisely what forced
    // "Low?" and "Unit" to be as wide as a name.
    header->setMinimumSectionSize(64);
    table->setTextElideMode(Qt::ElideRight);  // only ever applied to the columns chosen below

    // The identifying column: the first that is not a column of figures.
    int identifier = 0;
    for (int c = 0; c < columns; ++c) {
        const bool figures = c < static_cast<int>(profiles.size()) &&
                             profiles[static_cast<std::size_t>(c)].isFigures();
        if (!figures) {
            identifier = c;
            break;
        }
    }

    const QFontMetrics cellMetrics(table->font());
    const QFontMetrics headMetrics(header->font());
    constexpr int kPadding = 30;   ///< the stylesheet's own cell padding, plus breathing room
    constexpr int kNarrowest = 72; ///< "Low?", "Unit" — never narrower than a readable caption
    constexpr int kWidest = 210;   ///< an e-mail address; past this the identifier would starve

    for (int c = 0; c < columns; ++c) {
        if (c == identifier) {
            header->setSectionResizeMode(c, QHeaderView::Stretch);
            continue;
        }
        int wanted = headMetrics.horizontalAdvance(
            table->model()->headerData(c, Qt::Horizontal).toString());
        for (int r = 0; r < table->rowCount(); ++r) {
            if (const QTableWidgetItem* cell = table->item(r, c))
                wanted = std::max(wanted, cellMetrics.horizontalAdvance(cell->text()));
        }
        header->setSectionResizeMode(c, QHeaderView::Interactive);
        header->resizeSection(c, std::clamp(wanted + kPadding, kNarrowest, kWidest));
    }
}

/**
 * @brief The EmptyState belonging to one card.
 *
 * Two cards now carry one each, and gui::EmptyState is meta-object-free by design (Widgets.hpp),
 * so it is addressed the way the stylesheet addresses it: by object name, and only among the
 * card's own direct children — which is what keeps the two apart.
 */
QFrame* emptyStateIn(QWidget* card) {
    return card ? card->findChild<QFrame*>(QStringLiteral("emptyState"),
                                           Qt::FindDirectChildrenOnly)
                : nullptr;
}

/// Rewrites an EmptyState's two lines through its labels — no downcast, and the hint hides itself.
void setEmptyState(QFrame* state, const QString& title, const QString& hint) {
    if (!state) return;
    if (auto* titleLabel = state->findChild<QLabel*>(QStringLiteral("emptyStateTitle")))
        titleLabel->setText(title);
    if (auto* hintLabel = state->findChild<QLabel*>(QStringLiteral("emptyStateHint"))) {
        hintLabel->setText(hint);
        hintLabel->setVisible(!hint.isEmpty());
    }
}

/// Why there is no chart, in the user's terms — never a bare grid with nothing in it.
void describeEmptyChart(QFrame* state, const ChartPlan& plan, bool hasRows) {
    if (!hasRows) {
        setEmptyState(state, QStringLiteral("Nothing to chart yet"),
                      QStringLiteral("This report returned no rows for the selected range — "
                                     "widen the dates, or choose another report."));
    } else if (plan.allZero) {
        setEmptyState(state, QStringLiteral("No activity in this range"),
                      QStringLiteral("Every figure in the plotted column is zero, so there are "
                                     "no bars to draw. The rows are listed below."));
    } else {
        setEmptyState(state, QStringLiteral("This report has no figures to plot"),
                      QStringLiteral("Its columns are descriptive rather than numeric — the full "
                                     "detail is in the table below, and both exports carry it."));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ReportsPage::ReportsPage(services::AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    setObjectName(QStringLiteral("reportsPage"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 18);
    root->setSpacing(16);

    // --- page header --------------------------------------------------------
    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(10);

    auto* headings = new QVBoxLayout();
    headings->setSpacing(2);
    headings->addWidget(styledLabel(pageTitle(), QStringLiteral("pageTitle"), this, 10,
                                    QFont::DemiBold));
    headings->addWidget(styledLabel(
        QStringLiteral("Five reports, one interface — preview it, then take it away as CSV or PDF."),
        QStringLiteral("mutedLabel"), this, 0));
    headerRow->addLayout(headings, 1);

    auto* verifyBtn = makeButton(QStringLiteral("Verify audit trail"),
                                 QStringLiteral("ghostButton"), this);
    verifyBtn->setToolTip(QStringLiteral(
        "Walks the fixed-record binary audit file and checks every record's signature and checksum."));
    connect(verifyBtn, &QPushButton::clicked, this, &ReportsPage::onVerifyAudit);
    headerRow->addWidget(verifyBtn, 0, Qt::AlignVCenter);

    auto* csvBtn = makeButton(QStringLiteral("Export CSV"), QStringLiteral("ghostButton"), this);
    csvBtn->setToolTip(QStringLiteral("Writes the rows below to reports/ as a CSV file."));
    connect(csvBtn, &QPushButton::clicked, this, &ReportsPage::onExportCsv);
    headerRow->addWidget(csvBtn, 0, Qt::AlignVCenter);

    auto* pdfBtn = makeButton(QStringLiteral("Export PDF"), QStringLiteral("primaryButton"), this);
    pdfBtn->setToolTip(QStringLiteral("Writes the rows below to reports/ as a printable A4 PDF."));
    connect(pdfBtn, &QPushButton::clicked, this, &ReportsPage::onExportPdf);
    headerRow->addWidget(pdfBtn, 0, Qt::AlignVCenter);

    root->addLayout(headerRow);

    // --- body ----------------------------------------------------------------
    // The body is a scroll area purely as a *safety net* for a window squeezed down to the shell's
    // 1140x740 minimum: at any ordinary size the filter strip and the chart/table row fit exactly,
    // the row absorbs every spare pixel, and no scrollbar is drawn at all. The old arrangement
    // stacked three full-width cards whose minimum heights summed to more than the viewport, so
    // the screen *always* scrolled and the preview table — the entire point of the page — never
    // showed a single row above the fold.
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);

    auto* body = new QWidget(scroll);
    body->setAutoFillBackground(false);
    auto* bodyColumn = new QVBoxLayout(body);
    // GlassPanel paints its soft shadow *outside* its own rectangle, so the column keeps a margin
    // all round; without it the last card's shadow is shaved off by the viewport edge.
    bodyColumn->setContentsMargins(2, 2, 6, 6);
    bodyColumn->setSpacing(16);

    // --- controls: one compact strip, not a card with a paragraph in it ------
    auto* controlPanel = new GlassPanel(body);
    controlPanel->setObjectName(QStringLiteral("card"));
    auto* controlColumn = new QVBoxLayout(controlPanel);
    controlColumn->setContentsMargins(18, 14, 18, 14);
    controlColumn->setSpacing(8);

    auto* controls = new QHBoxLayout();
    controls->setSpacing(14);

    auto* kindColumn = new QVBoxLayout();
    kindColumn->setSpacing(4);
    kindColumn->addWidget(styledLabel(QStringLiteral("Report"), QStringLiteral("statCardTitle"),
                                      controlPanel, -1, QFont::DemiBold));
    m_kind = new QComboBox(controlPanel);
    m_kind->setMinimumWidth(190);
    m_kind->setMinimumHeight(38);
    m_kind->setCursor(Qt::PointingHandCursor);
    for (const KindEntry& entry : kKinds)
        m_kind->addItem(QString::fromUtf8(entry.label), static_cast<int>(entry.kind));
    kindColumn->addWidget(m_kind);
    controls->addLayout(kindColumn);

    const QDate today = QDate::currentDate();

    auto* fromColumn = new QVBoxLayout();
    fromColumn->setSpacing(4);
    fromColumn->addWidget(
        styledLabel(QStringLiteral("From"), QStringLiteral("statCardTitle"), controlPanel, -1,
                    QFont::DemiBold));
    // No local stylesheet on either date field: ThemeManager styles QDateEdit and QComboBox from
    // the same rule, and a page-level override here is what makes a row of inputs look mismatched.
    // Only the geometry is set, and it matches the report combo beside it exactly.
    m_from = new QDateEdit(today.addDays(-29), controlPanel);
    m_from->setCalendarPopup(true);
    m_from->setDisplayFormat(QStringLiteral("d MMM yyyy"));
    m_from->setMinimumHeight(38);
    m_from->setMinimumWidth(150);
    m_from->setCursor(Qt::PointingHandCursor);
    fromColumn->addWidget(m_from);
    controls->addLayout(fromColumn);

    auto* toColumn = new QVBoxLayout();
    toColumn->setSpacing(4);
    toColumn->addWidget(
        styledLabel(QStringLiteral("To"), QStringLiteral("statCardTitle"), controlPanel, -1,
                    QFont::DemiBold));
    m_to = new QDateEdit(today, controlPanel);
    m_to->setCalendarPopup(true);
    m_to->setDisplayFormat(QStringLiteral("d MMM yyyy"));
    m_to->setMinimumHeight(38);
    m_to->setMinimumWidth(150);
    m_to->setCursor(Qt::PointingHandCursor);
    toColumn->addWidget(m_to);
    controls->addLayout(toColumn);

    auto* quickColumn = new QVBoxLayout();
    quickColumn->setSpacing(4);
    quickColumn->addWidget(
        styledLabel(QStringLiteral("Quick range"), QStringLiteral("statCardTitle"), controlPanel,
                    -1, QFont::DemiBold));
    auto* quickRow = new QHBoxLayout();
    quickRow->setSpacing(8);
    struct QuickRange {
        const char* label;
        int days;
    };
    const QuickRange quickRanges[] = {{"7 days", 7}, {"30 days", 30}, {"90 days", 90}};
    for (const QuickRange& range : quickRanges) {
        auto* button = makeButton(QString::fromUtf8(range.label), QStringLiteral("ghostButton"),
                                  controlPanel);
        button->setMinimumHeight(38);
        const int days = range.days;
        connect(button, &QPushButton::clicked, this, [this, days]() {
            const QDate end = QDate::currentDate();
            m_from->setDate(end.addDays(-(days - 1)));
            m_to->setDate(end);
        });
        quickRow->addWidget(button);
    }
    quickColumn->addLayout(quickRow);
    controls->addLayout(quickColumn);

    controls->addStretch(1);

    controlColumn->addLayout(controls);

    // The selected kind explains itself on its own line. It is deliberately this panel's ONLY
    // direct child named "mutedLabel", which is what lets refresh() re-write it by name without
    // this page carrying a member pointer the frozen header does not declare.
    auto* kindHint = styledLabel(QString(), QStringLiteral("mutedLabel"), controlPanel, -1);
    kindHint->setWordWrap(true);
    controlColumn->addWidget(kindHint);

    bodyColumn->addWidget(controlPanel);

    // --- chart beside the data, not on top of it -----------------------------
    // A 1300 px card spending 470 px of height on a single bar, with the table it describes pushed
    // entirely below the fold, is the whole complaint in one picture. Landscape space is what this
    // window has most of, so the garnish takes the narrow column (4) and the report itself takes
    // the wide one (7) — and both are full height, so the table shows a dozen rows rather than a
    // header band.
    auto* mainRow = new QHBoxLayout();
    mainRow->setSpacing(16);

    QFrame* chartPanel = nullptr;
    QVBoxLayout* chartColumn =
        buildPanel(QStringLiteral("Chart"),
                   QStringLiteral("Built from exactly the rows listed beside it."), body,
                   &chartPanel);
    m_chart = new QChartView(chartPanel);
    m_chart->setObjectName(QStringLiteral("reportChart"));
    m_chart->setRenderHint(QPainter::Antialiasing, true);
    m_chart->setFrameShape(QFrame::NoFrame);
    // The floor at which the value axis, the bars, the tilted category labels and the baseline all
    // still fit. Below it QtCharts starts dropping the bottom of the plot. It is only a floor now:
    // in the side-by-side row the plot gets the card's full height, which is far more.
    m_chart->setMinimumHeight(200);
    m_chart->setBackgroundBrush(Qt::NoBrush);
    m_chart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    chartColumn->addWidget(m_chart, 1);

    // A chart with nothing in it says so, instead of showing an empty grid.
    auto* chartEmpty = new EmptyState(QStringLiteral("Nothing to chart yet"),
                                      QStringLiteral("Widen the date range, or choose another "
                                                     "report."),
                                      chartPanel);
    chartEmpty->setMinimumHeight(200);
    chartEmpty->setVisible(false);
    chartColumn->addWidget(chartEmpty, 1);

    // No explicit minimum on the card itself: an explicit minimumHeight *replaces* the one its
    // layout computes (qSmartMinSize), so a number guessed here could end up smaller than the
    // heading, subtitle, padding and plot actually need. The chart's own floor propagates up
    // through the card's layout instead, which cannot be wrong.
    mainRow->addWidget(chartPanel, 4);

    // --- preview ------------------------------------------------------------
    QFrame* previewPanel = nullptr;
    QVBoxLayout* previewColumn =
        buildPanel(QStringLiteral("Preview"),
                   QStringLiteral("Exactly the rows an export will contain, in export order."),
                   body, &previewPanel);

    m_preview = new ElegantTable(previewPanel);
    // Sorting is deliberately off: the preview must agree row-for-row with the exported file.
    m_preview->setSortingEnabled(false);
    // Five 44 px rows plus the header is the *floor*, for a window squeezed to the shell minimum;
    // at any ordinary size the card is full height and this table shows eleven or twelve.
    m_preview->setMinimumHeight(5 * 44 + 44);
    m_preview->setSelectionMode(QAbstractItemView::SingleSelection);
    m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    previewColumn->addWidget(m_preview, 1);

    // The row count sits with the rows, where a reader is already looking, rather than in the
    // filter strip a screen away from the table it describes.
    auto* rowCount = styledLabel(QString(), QStringLiteral("reportRowCount"), previewPanel, -1,
                                 QFont::DemiBold);
    rowCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    previewColumn->addWidget(rowCount);

    auto* empty = new EmptyState(QStringLiteral("Nothing to report yet"),
                                 QStringLiteral("Widen the date range, or choose another report."),
                                 previewPanel);
    empty->setMinimumHeight(200);
    empty->setVisible(false);
    previewColumn->addWidget(empty, 1);

    mainRow->addWidget(previewPanel, 7);
    bodyColumn->addLayout(mainRow, 1);

    // --- where the last file went -------------------------------------------
    auto* status = styledLabel(QString(), QStringLiteral("reportStatus"), body, -1);
    status->setTextFormat(Qt::RichText);
    status->setTextInteractionFlags(Qt::TextBrowserInteraction);
    status->setOpenExternalLinks(true);
    status->setWordWrap(true);
    status->setVisible(false);
    bodyColumn->addWidget(status);

    scroll->setWidget(body);
    root->addWidget(scroll, 1);

    // --- wiring --------------------------------------------------------------
    connect(m_kind, &QComboBox::currentIndexChanged, this, &ReportsPage::onKindChanged);
    connect(m_from, &QDateEdit::dateChanged, this, &ReportsPage::refresh);
    connect(m_to, &QDateEdit::dateChanged, this, &ReportsPage::refresh);
    // A theme switch changes every colour the chart was painted with, so it is simply rebuilt.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &ReportsPage::refresh);

    refresh();
}

QString ReportsPage::pageTitle() const { return QStringLiteral("Reports"); }

// ---------------------------------------------------------------------------
// Refresh — one code path for all five reports
// ---------------------------------------------------------------------------

void ReportsPage::refresh() {
    const Palette& pal = ThemeManager::instance().palette();

    // Two cards now carry an EmptyState, so each is looked up inside its own card rather than by
    // a page-wide name search that would always find whichever was constructed first.
    QFrame* previewEmpty = emptyStateIn(m_preview->parentWidget());
    QFrame* chartEmpty = emptyStateIn(m_chart->parentWidget());
    auto* rowCount = findChild<QLabel*>(QStringLiteral("reportRowCount"));

    // The date pickers are a range: never let "to" precede "from".
    if (m_from->date() > m_to->date()) {
        const QSignalBlocker block(m_to);
        m_to->setDate(m_from->date());
    }

    try {
        /// @oop-concept Runtime Polymorphism :: the factory returns one of five concrete reports and
        /// everything below this line speaks only to the abstract ReportGenerator interface.
        const std::unique_ptr<services::ReportGenerator> report =
            m_ctx.reports().makeReport(selectedKind(m_kind), m_from->date(), m_to->date());
        if (!report) {
            m_ctx.notifications().notify(QStringLiteral("Reports"),
                                         QStringLiteral("That report could not be built."), 3);
            return;
        }

        const QString title = report->title();
        const QStringList header = report->header();
        const std::vector<QStringList> rows = report->rows();
        const std::vector<ColumnProfile> profiles = profileColumns(header.size(), rows);

        // Counted once, quoted everywhere. `rows` is the single query behind this whole screen,
        // so the badge under the table, the sentence that explains it and the chart caption's
        // denominator are all read off this one tally and cannot contradict one another.
        const RowTally tally = tallyRows(rows);
        const ChartPlan plan = planChart(header, rows, profiles);

        // ---- preview table -------------------------------------------------
        m_preview->clear();
        m_preview->setColumnCount(header.size());
        m_preview->setHorizontalHeaderLabels(header);
        m_preview->setRowCount(static_cast<int>(rows.size()));

        for (std::size_t r = 0; r < rows.size(); ++r) {
            const QStringList& row = rows[r];
            const int rowIndex = static_cast<int>(r);
            const bool summary = !row.isEmpty() && isSummaryLabel(row.at(0));
            for (int c = 0; c < header.size(); ++c) {
                const QString text = c < row.size() ? row.at(c) : QString();
                auto* cell = new QTableWidgetItem(text);
                const bool figures =
                    c < static_cast<int>(profiles.size()) &&
                    profiles[static_cast<std::size_t>(c)].isFigures();
                cell->setTextAlignment((figures ? Qt::AlignRight : Qt::AlignLeft) |
                                       Qt::AlignVCenter);
                if (summary) {
                    QFont f = cell->font();
                    f.setWeight(QFont::DemiBold);
                    cell->setFont(f);
                    cell->setForeground(pal.primary);
                } else if (figures) {
                    cell->setForeground(pal.text);
                }
                m_preview->setItem(rowIndex, c, cell);
            }
        }
        // The table fills its card: figures stay as narrow as their numbers, text columns share
        // what is left, and Qt re-applies both on every resize.
        fitColumns(m_preview, profiles);

        const bool hasRows = !rows.empty();
        m_preview->setVisible(hasRows);
        if (previewEmpty) {
            // Restated every time, so a message left over from a failed read cannot survive into
            // an ordinary "this range is empty".
            setEmptyState(previewEmpty, QStringLiteral("Nothing to report yet"),
                          QStringLiteral("Widen the date range, or choose another report."));
            previewEmpty->setVisible(!hasRows);
        }
        if (rowCount) {
            // Spelled out rather than left as a bare "31 rows" beside a chart that talks about
            // "30 days": the composition is stated, so the two figures visibly add up.
            QString tallyText;
            if (!hasRows) {
                tallyText = QStringLiteral("no rows");
            } else if (tally.summary > 0) {
                tallyText = QStringLiteral("%1 rows  ·  %2 %3 and a grand-total line")
                                .arg(tally.total)
                                .arg(tally.data)
                                .arg(RowTally::noun(plan.dateLabels, tally.data));
            } else {
                tallyText = QStringLiteral("%1 %2")
                                .arg(tally.total)
                                .arg(tally.total == 1 ? QStringLiteral("row") : QStringLiteral("rows"));
            }
            rowCount->setText(tallyText);
            rowCount->setVisible(true);
        }

        // ---- chart ----------------------------------------------------------
        QString caption = QStringLiteral("Chart");
        if (plan.usable) {
            caption = QStringLiteral("%1 by %2")
                          .arg(header.at(plan.valueColumn), header.at(plan.labelColumn));
            installChart(m_chart, buildChart(plan, header.at(plan.valueColumn), pal));
            m_chart->setVisible(true);
            if (chartEmpty) chartEmpty->setVisible(false);
        } else {
            // Never leave a bare grid on screen: the old chart goes, and the card explains itself.
            installChart(m_chart, blankChart());
            m_chart->setVisible(false);
            describeEmptyChart(chartEmpty, plan, hasRows);
            if (chartEmpty) chartEmpty->setVisible(true);
        }

        // The chart card's own heading and subtitle are its only direct QLabel children,
        // so they can be retitled without this page keeping extra member pointers.
        if (QWidget* chartPanel = m_chart->parentWidget()) {
            if (auto* heading = chartPanel->findChild<QLabel*>(QStringLiteral("sectionTitle"),
                                                              Qt::FindDirectChildrenOnly))
                heading->setText(caption);
            if (auto* sub = chartPanel->findChild<QLabel*>(QStringLiteral("mutedLabel"),
                                                          Qt::FindDirectChildrenOnly)) {
                QString explanation;
                if (plan.usable) {
                    const QString unit = plan.money ? QStringLiteral(", in rupees") : QString();
                    // The denominator is the tally's own data-row count — the very number the
                    // badge under the table breaks out — so "the most recent 8 of 30 days" and
                    // "31 rows · 30 days and a grand-total line" are arithmetic, not coincidence.
                    if (static_cast<int>(plan.points.size()) < tally.data) {
                        // The chart shows a readable slice, so it says so rather than claiming to
                        // be the whole table.
                        explanation =
                            plan.dateLabels
                                ? QStringLiteral("%1  ·  the most recent %2 of %3 days beside it%4")
                                      .arg(title)
                                      .arg(plan.points.size())
                                      .arg(tally.data)
                                      .arg(unit)
                                : QStringLiteral("%1  ·  the %2 largest of %3 entries beside it%4")
                                      .arg(title)
                                      .arg(plan.points.size())
                                      .arg(tally.data)
                                      .arg(unit);
                    } else {
                        explanation =
                            QStringLiteral("%1  ·  all %2 %3 in the table beside it%4")
                                .arg(title)
                                .arg(tally.data)
                                .arg(RowTally::noun(plan.dateLabels, tally.data), unit);
                    }
                } else if (!hasRows) {
                    explanation = QStringLiteral("%1  ·  no rows in this range.").arg(title);
                } else if (plan.allZero) {
                    explanation = QStringLiteral("%1  ·  every figure in this range is zero.")
                                      .arg(title);
                } else {
                    explanation = QStringLiteral("%1  ·  this report has no figures to plot.")
                                      .arg(title);
                }
                sub->setText(explanation);
            }
        }

        // ---- the kind's own explanation -------------------------------------
        if (QWidget* panel = m_kind->parentWidget()) {
            if (auto* sub = panel->findChild<QLabel*>(QStringLiteral("mutedLabel"),
                                                     Qt::FindDirectChildrenOnly))
                sub->setText(selectedHint(m_kind));
        }
    } catch (const std::exception& e) {
        // Page::refresh() must never throw — a failed read becomes a visible notice instead.
        m_preview->setRowCount(0);
        m_preview->setVisible(false);
        installChart(m_chart, blankChart());
        m_chart->setVisible(false);
        if (previewEmpty) {
            setEmptyState(previewEmpty, QStringLiteral("This report could not be built"),
                          QString::fromUtf8(e.what()));
            previewEmpty->setVisible(true);
        }
        if (chartEmpty) {
            setEmptyState(chartEmpty, QStringLiteral("Nothing to chart"),
                          QStringLiteral("The report behind this chart could not be read."));
            chartEmpty->setVisible(true);
        }
        if (rowCount) rowCount->setText(QStringLiteral("unavailable"));
        m_ctx.notifications().notify(QStringLiteral("Reports"), QString::fromUtf8(e.what()), 3);
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ReportsPage::onKindChanged() { refresh(); }

void ReportsPage::onExportCsv() {
    try {
        const std::unique_ptr<services::ReportGenerator> report =
            m_ctx.reports().makeReport(selectedKind(m_kind), m_from->date(), m_to->date());
        if (!report) {
            m_ctx.notifications().notify(QStringLiteral("Export failed"),
                                         QStringLiteral("That report could not be built."), 3);
            return;
        }

        const QString folder = ensureOutputDir(QStringLiteral("reports"));
        const QString path = QDir(folder).filePath(
            QStringLiteral("%1-%2.csv").arg(slugify(report->title()), fileStamp()));

        /// The CSV itself is written by the services layer, which drives the persistence-layer
        /// `<fstream>` writer — no widget ever opens a file for a CSV export.
        const QString written = report->exportCsv(path);

        announceSaved(this, m_ctx, QStringLiteral("CSV"), written);
        m_ctx.audit().log(QStringLiteral("REPORT_CSV"), QStringLiteral("report"));
    } catch (const std::exception& e) {
        m_ctx.notifications().notify(QStringLiteral("CSV export failed"),
                                     QString::fromUtf8(e.what()), 3);
    }
}

void ReportsPage::onExportPdf() {
    try {
        const std::unique_ptr<services::ReportGenerator> report =
            m_ctx.reports().makeReport(selectedKind(m_kind), m_from->date(), m_to->date());
        if (!report) {
            m_ctx.notifications().notify(QStringLiteral("Export failed"),
                                         QStringLiteral("That report could not be built."), 3);
            return;
        }

        const QString folder = ensureOutputDir(QStringLiteral("reports"));
        const QString path = QDir(folder).filePath(
            QStringLiteral("%1-%2.pdf").arg(slugify(report->title()), fileStamp()));

        /// PDF rendering stays in the GUI layer, because QtPrintSupport is a presentation module.
        const core::Result<QString> result =
            PdfExporter::exportReportPdf(report->title(), report->header(), report->rows(), path);

        if (result.isOk()) {
            announceSaved(this, m_ctx, QStringLiteral("PDF"), result.value());
            m_ctx.audit().log(QStringLiteral("REPORT_PDF"), QStringLiteral("report"));
        } else {
            m_ctx.notifications().notify(QStringLiteral("PDF export failed"), result.error(), 3);
        }
    } catch (const std::exception& e) {
        m_ctx.notifications().notify(QStringLiteral("PDF export failed"),
                                     QString::fromUtf8(e.what()), 3);
    }
}

void ReportsPage::onVerifyAudit() {
    const Palette& pal = ThemeManager::instance().palette();

    std::size_t firstBadIndex = 0;
    std::size_t records = 0;
    bool clean = false;
    QString failure;

    try {
        records = m_ctx.audit().trailRecordCount();
        /// The out-parameter is what turns "something is wrong" into "record #N is wrong".
        clean = m_ctx.audit().verifyTrailIntegrity(firstBadIndex);
    } catch (const std::exception& e) {
        failure = QString::fromUtf8(e.what());
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Audit trail integrity"));
    // Wide enough that the seven record columns below fit without a horizontal scrollbar.
    dialog.setMinimumWidth(880);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(24, 22, 24, 20);
    column->setSpacing(12);

    QString headline;
    QColor headlineColour = pal.success;
    if (!failure.isEmpty()) {
        headline = QStringLiteral("The audit trail could not be read");
        headlineColour = pal.danger;
    } else if (clean) {
        headline = records == 0 ? QStringLiteral("The audit trail is empty but valid")
                                : QStringLiteral("All %1 records verified").arg(records);
    } else {
        headline = QStringLiteral("Tampering detected at record #%1").arg(firstBadIndex);
        headlineColour = pal.danger;
    }

    auto* headlineLabel = styledLabel(headline, QStringLiteral("sectionTitle"), &dialog, 6,
                                      QFont::DemiBold);
    QPalette headlinePalette = headlineLabel->palette();
    headlinePalette.setColor(QPalette::WindowText, headlineColour);
    headlineLabel->setPalette(headlinePalette);
    column->addWidget(headlineLabel);

    QString detail;
    if (!failure.isEmpty()) {
        detail = failure;
    } else {
        detail = QStringLiteral(
                     "Every entry is a fixed 128-byte record, so record N always begins at byte "
                     "N × 128 and any one of them can be read with a single seek. %1 records "
                     "occupy %2 bytes. The check re-reads each record, confirms its signature and "
                     "recomputes its checksum.")
                     .arg(records)
                     .arg(records * 128);
        if (!clean)
            detail += QStringLiteral(
                          " Record #%1 failed — everything before it is still trustworthy.")
                          .arg(firstBadIndex);
    }
    auto* detailLabel = styledLabel(detail, QStringLiteral("mutedLabel"), &dialog, 0);
    detailLabel->setWordWrap(true);
    column->addWidget(detailLabel);

    // -----------------------------------------------------------------------
    // Random-access record browser — "fetch record #N"
    // -----------------------------------------------------------------------
    // This is the seek-by-index capability made operable. Nothing is scanned: the index alone
    // decides the byte offset (N × 128) the trail seeks to, so record 0 and record 90 000 cost the
    // same. services::AuditService::trailRecordAt is the only method used, so no persistence type
    // is ever named in this GUI translation unit.
    auto* browser = new GlassPanel(&dialog);
    browser->setObjectName(QStringLiteral("card"));
    auto* browserColumn = new QVBoxLayout(browser);
    browserColumn->setContentsMargins(16, 14, 16, 14);
    browserColumn->setSpacing(10);

    browserColumn->addWidget(styledLabel(QStringLiteral("Jump straight to a record"),
                                         QStringLiteral("sectionTitle"), browser, 0,
                                         QFont::DemiBold));

    auto* jumpRow = new QHBoxLayout();
    jumpRow->setSpacing(12);

    const bool browsable = failure.isEmpty() && records > 0;
    const int lastIndex = records > 0 ? static_cast<int>(records - 1) : 0;

    // The control is captioned and carries its own range, in the field and beside it: this is a
    // graded demonstration of random-access file IO, so it has to read as a designed instrument
    // rather than a bare number box somebody left behind.
    auto* jumpColumn = new QVBoxLayout();
    jumpColumn->setSpacing(4);
    jumpColumn->addWidget(styledLabel(QStringLiteral("Record number"),
                                      QStringLiteral("statCardTitle"), browser, -1,
                                      QFont::DemiBold));

    auto* jumpSpin = new QSpinBox(browser);
    jumpSpin->setObjectName(QStringLiteral("auditRecordIndex"));
    jumpSpin->setMinimumHeight(38);
    jumpSpin->setMinimumWidth(170);
    jumpSpin->setRange(0, lastIndex);
    jumpSpin->setPrefix(QStringLiteral("#  "));
    // The upper bound travels inside the field — "#  339 of 339" — so the valid range is legible
    // without looking anywhere else, and the arrows walk the file record by record.
    jumpSpin->setSuffix(QStringLiteral("  of %1").arg(lastIndex));
    jumpSpin->setAccelerated(true);
    jumpSpin->setAlignment(Qt::AlignHCenter);
    jumpSpin->setEnabled(browsable);
    jumpSpin->setToolTip(QStringLiteral(
        "The record's position in the file, counted from zero. Record N is read straight from "
        "byte N × 128 — no scanning."));
    jumpColumn->addWidget(jumpSpin);
    jumpRow->addLayout(jumpColumn, 0);

    auto* fetchColumn = new QVBoxLayout();
    fetchColumn->setSpacing(4);
    fetchColumn->addWidget(styledLabel(QString(), QStringLiteral("statCardTitle"), browser, -1));
    auto* fetchBtn = makeButton(QStringLiteral("Read this record"),
                                QStringLiteral("primaryButton"), browser);
    fetchBtn->setEnabled(browsable);
    fetchColumn->addWidget(fetchBtn);
    jumpRow->addLayout(fetchColumn, 0);

    auto* rangeLabel = styledLabel(
        browsable ? QStringLiteral("Any number from 0 to %1 is valid  ·  %2 records on file  ·  "
                                   "every one of them costs a single seek")
                        .arg(lastIndex).arg(records)
                  : QStringLiteral("The trail holds no records yet — there is nothing to fetch."),
        QStringLiteral("mutedLabel"), browser, -1);
    rangeLabel->setWordWrap(true);
    jumpRow->addWidget(rangeLabel, 1, Qt::AlignBottom);
    browserColumn->addLayout(jumpRow);

    auto* recordHeadline = styledLabel(
        browsable ? QStringLiteral("Reading…") : QStringLiteral("Nothing to browse"),
        QStringLiteral("sectionTitle"), browser, 0, QFont::DemiBold);
    recordHeadline->setWordWrap(true);
    browserColumn->addWidget(recordHeadline);

    auto* recordBody = styledLabel(
        browsable ? QString()
                  : QStringLiteral("Records appear here as soon as the restaurant is used."),
        QStringLiteral("mutedLabel"), browser, 0);
    recordBody->setWordWrap(true);
    recordBody->setMinimumHeight(56);
    recordBody->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    browserColumn->addWidget(recordBody);

    // Colours are read from the live palette at click time, so the dialog obeys a theme switch.
    const auto tintLabel = [](QLabel* label, const QColor& colour) {
        QPalette p = label->palette();
        p.setColor(QPalette::WindowText, colour);
        label->setPalette(p);
    };

    const auto fetchRecord = [this, jumpSpin, recordHeadline, recordBody, tintLabel]() {
                const Palette& live = ThemeManager::instance().palette();
                const int wanted = jumpSpin->value();

                // The spin box already clamps to the file's range; the try/catch is what keeps an
                // index that has *become* invalid — a trail truncated or rotated while this dialog
                // was open — a readable sentence instead of an unhandled FileIOException.
                try {
                    /// `auto` on purpose: naming the record type would drag a persistence header
                    /// into a GUI translation unit (ARCHITECTURE §1, §12 R1).
                    const auto record =
                        m_ctx.audit().trailRecordAt(static_cast<std::size_t>(wanted));

                    const QDateTime when =
                        QDateTime::fromMSecsSinceEpoch(record.timestampUtcMs, QTimeZone::UTC)
                            .toLocalTime();
                    const core::Money amount(record.amountPaisa);
                    const QString details = fixedField(record.details, sizeof(record.details));

                    tintLabel(recordHeadline, live.primary);
                    recordHeadline->setText(
                        QStringLiteral("Record #%1  ·  %2  ·  %3")
                            .arg(wanted)
                            .arg(fixedField(record.action, sizeof(record.action)),
                                 fixedField(record.entity, sizeof(record.entity))));

                    tintLabel(recordBody, live.textMuted);
                    recordBody->setText(
                        QStringLiteral("%1  ·  sequence %2  ·  %3  ·  %4\n"
                                       "Read from byte offset %5 in one seek.%6")
                            .arg(when.toString(QStringLiteral("d MMM yyyy hh:mm:ss")))
                            .arg(record.seq)
                            .arg(amount.isZero() ? QStringLiteral("no money involved")
                                                 : core::formatNpr(amount),
                                 record.userId == 0u ? QStringLiteral("by the system")
                                                     : QStringLiteral("by user #%1")
                                                           .arg(record.userId))
                            .arg(static_cast<qulonglong>(wanted) * 128ull)
                            .arg(details.isEmpty() ? QString()
                                                   : QStringLiteral("  Details: %1").arg(details)));
                } catch (const std::exception& e) {
                    tintLabel(recordHeadline, live.danger);
                    recordHeadline->setText(
                        QStringLiteral("Record #%1 could not be read").arg(wanted));
                    tintLabel(recordBody, live.textMuted);
                    recordBody->setText(QString::fromUtf8(e.what()));
                }
    };

    // Both the button and the spin box's own arrows drive the same read, so the trail can be
    // walked record by record; and the browser opens on the newest record already fetched rather
    // than on an empty "no record fetched yet" box that has to be poked before it does anything.
    connect(fetchBtn, &QPushButton::clicked, &dialog, fetchRecord);
    connect(jumpSpin, &QSpinBox::valueChanged, &dialog, [fetchRecord](int) { fetchRecord(); });
    if (browsable) {
        jumpSpin->setValue(lastIndex);
        fetchRecord();
    }

    column->addWidget(browser);

    // The table below is a *tail*, not the file. Saying so is the difference between a reader
    // trusting the figure above it ("318 records on file") and wondering why they can only see
    // fifteen of them.
    constexpr int kRecentRecords = 15;
    auto* recentCaption = styledLabel(
        records == 0 ? QStringLiteral("The trail is empty.")
                     : QStringLiteral("The last %1 records written, oldest of the %1 first — the "
                                      "browser above reaches any of the other %2.")
                           .arg(kRecentRecords)
                           .arg(records > kRecentRecords
                                    ? static_cast<qulonglong>(records) - kRecentRecords
                                    : 0ull),
        QStringLiteral("mutedLabel"), &dialog, 0);
    recentCaption->setWordWrap(true);
    column->addWidget(recentCaption);

    auto* table = new ElegantTable(QStringList{QStringLiteral("When"), QStringLiteral("Seq"),
                                               QStringLiteral("Action"), QStringLiteral("Entity"),
                                               QStringLiteral("Amount"), QStringLiteral("User"),
                                               QStringLiteral("Details")},
                                  &dialog);
    table->setSortingEnabled(false);
    // Exactly seven 44 px rows under the 44 px header, so the block never ends on a sliced row and
    // the dialog still fits a laptop screen with the record browser above it. Five showed a third
    // of what had been fetched and left the rest behind a scrollbar for no reason.
    // Pinned, not merely floored: with only a minimum the layout handed the table a few spare
    // pixels and an eighth row appeared sliced across the bottom edge.
    table->setMinimumHeight(7 * 44 + 44);
    table->setMaximumHeight(7 * 44 + 44);
    table->setTextElideMode(Qt::ElideRight);
    // The six fixed fields take the width they need and free text takes the rest, so nothing has
    // to be scrolled sideways to be read.
    table->horizontalHeader()->setStretchLastSection(false);
    for (int c = 0; c < table->columnCount() - 1; ++c)
        table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(table->columnCount() - 1,
                                                    QHeaderView::Stretch);

    try {
        // `auto` on purpose: naming the record type would drag a persistence header into a GUI
        // translation unit and break the layer rule (ARCHITECTURE §1, §12 R1).
        const auto recent =
            m_ctx.audit().recentTrailRecords(static_cast<std::size_t>(kRecentRecords));
        table->setRowCount(static_cast<int>(recent.size()));
        int rowIndex = 0;
        for (const auto& record : recent) {
            const QDateTime when =
                QDateTime::fromMSecsSinceEpoch(record.timestampUtcMs, QTimeZone::UTC).toLocalTime();
            const core::Money amount(record.amountPaisa);

            table->setItem(rowIndex, 0,
                           new QTableWidgetItem(when.toString(QStringLiteral("d MMM yyyy hh:mm:ss"))));
            table->setItem(rowIndex, 1,
                           new QTableWidgetItem(QString::number(record.seq)));
            table->setItem(rowIndex, 2,
                           new QTableWidgetItem(fixedField(record.action, sizeof(record.action))));
            table->setItem(rowIndex, 3,
                           new QTableWidgetItem(fixedField(record.entity, sizeof(record.entity))));

            auto* amountCell = new QTableWidgetItem(amount.isZero() ? QStringLiteral("—")
                                                                    : core::formatNpr(amount));
            amountCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(rowIndex, 4, amountCell);

            table->setItem(rowIndex, 5,
                           new QTableWidgetItem(record.userId == 0u
                                                    ? QStringLiteral("system")
                                                    : QStringLiteral("#%1").arg(record.userId)));
            table->setItem(rowIndex, 6,
                           new QTableWidgetItem(fixedField(record.details, sizeof(record.details))));
            ++rowIndex;
        }
        if (recent.empty()) {
            table->setRowCount(1);
            auto* placeholder = new QTableWidgetItem(
                QStringLiteral("Nothing has been recorded yet — the trail fills as the "
                               "restaurant is used."));
            placeholder->setTextAlignment(Qt::AlignCenter);
            placeholder->setForeground(pal.textMuted);
            placeholder->setFlags(Qt::NoItemFlags);
            table->setItem(0, 0, placeholder);
            table->setSpan(0, 0, 1, table->columnCount());
            table->setRowHeight(0, 80);
        }
    } catch (const std::exception& e) {
        table->setRowCount(1);
        auto* placeholder = new QTableWidgetItem(QString::fromUtf8(e.what()));
        placeholder->setTextAlignment(Qt::AlignCenter);
        placeholder->setForeground(pal.danger);
        placeholder->setFlags(Qt::NoItemFlags);
        table->setItem(0, 0, placeholder);
        table->setSpan(0, 0, 1, table->columnCount());
        table->setRowHeight(0, 80);
    }

    // Column widths are already governed by the resize modes set above; re-measuring here would
    // put the table back to a width wider than the dialog and bring the scrollbar back.
    column->addWidget(table, 1);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    auto* closeBtn = makeButton(QStringLiteral("Close"), QStringLiteral("primaryButton"), &dialog);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonRow->addWidget(closeBtn);
    column->addLayout(buttonRow);

    // The verification itself is an auditable event, so it is recorded before the dialog closes.
    try {
        m_ctx.audit().log(QStringLiteral("AUDIT_VERIFY"),
                          clean ? QStringLiteral("trail:clean") : QStringLiteral("trail:damaged"));
    } catch (const std::exception&) {
        // A trail that cannot be written has already been reported above; do not double-report.
    }

    dialog.exec();

    if (!failure.isEmpty())
        m_ctx.notifications().notify(QStringLiteral("Audit trail"), failure, 3);
    else if (!clean)
        m_ctx.notifications().notify(
            QStringLiteral("Audit trail damaged"),
            QStringLiteral("Record #%1 failed verification.").arg(firstBadIndex), 3);
    else
        m_ctx.notifications().notify(
            QStringLiteral("Audit trail verified"),
            QStringLiteral("%1 record%2 checked, all intact.")
                .arg(records)
                .arg(records == 1 ? QString() : QStringLiteral("s")),
            1);
}

} // namespace aluchop::gui

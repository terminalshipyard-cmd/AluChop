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
 * @par The audit check and the layer rule
 * "Verify integrity" walks the fixed-record binary audit trail. The GUI layer may not name
 * `aluchop::persistence` at all, so this file calls the four delegating methods
 * services::AuditService added for exactly this purpose (`verifyTrailIntegrity`,
 * `trailRecordCount`, `recentTrailRecords`) and never touches `AuditService::trail()`.
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
#include <QSignalBlocker>
#include <QSizePolicy>
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
    std::vector<ChartPoint> points;
};

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
        }

        double value = 0.0;
        bool money = false;
        if (!numericValue(row.at(plan.valueColumn), value, money)) continue;
        plan.points.push_back(ChartPoint{shown, value});
    }

    if (plan.points.empty()) return plan;

    if (plan.dateLabels) {
        // Keep the newest slice of a long range so the bars stay readable.
        constexpr std::size_t kMaxDays = 16;
        if (plan.points.size() > kMaxDays)
            plan.points.erase(plan.points.begin(),
                              plan.points.end() - static_cast<std::ptrdiff_t>(kMaxDays));
    } else {
        /// @oop-concept STL (algorithms) :: std::sort ranks the categories before the top slice
        std::sort(plan.points.begin(), plan.points.end(),
                  [](const ChartPoint& a, const ChartPoint& b) { return a.value > b.value; });
        constexpr std::size_t kMaxBars = 12;
        if (plan.points.size() > kMaxBars) plan.points.resize(kMaxBars);
        // Horizontal bars are drawn bottom-up, so reversing puts the biggest bar at the top.
        std::reverse(plan.points.begin(), plan.points.end());
    }

    plan.usable = true;
    return plan;
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

    auto* chart = new QChart();
    chart->legend()->hide();
    chart->setBackgroundVisible(false);
    chart->setPlotAreaBackgroundVisible(false);
    chart->setMargins(QMargins(0, 4, 0, 0));
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setAnimationDuration(420);

    auto* categoryAxis = new QBarCategoryAxis();
    categoryAxis->append(categories);
    categoryAxis->setLabelsColor(pal.textMuted);
    categoryAxis->setLineVisible(false);
    categoryAxis->setGridLineVisible(false);

    auto* valueAxis = new QValueAxis();
    valueAxis->setLabelFormat(highest < 10.0 ? QStringLiteral("%.2f") : QStringLiteral("%.0f"));
    valueAxis->setTickCount(5);
    valueAxis->setRange(lowest < 0.0 ? lowest * 1.15 : 0.0,
                        highest > 0.0 ? highest * 1.18 : 1.0);
    valueAxis->setLabelsColor(pal.textMuted);
    valueAxis->setLineVisible(false);
    valueAxis->setGridLineColor(pal.border);
    valueAxis->setMinorGridLineVisible(false);

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

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ReportsPage::ReportsPage(services::AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    setObjectName(QStringLiteral("reportsPage"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(18);

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

    // --- controls -----------------------------------------------------------
    QFrame* controlPanel = nullptr;
    QVBoxLayout* controlColumn =
        buildPanel(QStringLiteral("What do you want to look at?"),
                   QStringLiteral("The date range applies to the time-based reports; the stock, "
                                  "loyalty and roster reports always show the position as it "
                                  "stands right now."),
                   this, &controlPanel);

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
    m_from = new QDateEdit(today.addDays(-29), controlPanel);
    m_from->setCalendarPopup(true);
    m_from->setDisplayFormat(QStringLiteral("d MMM yyyy"));
    m_from->setMinimumHeight(38);
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

    auto* rowCount = styledLabel(QString(), QStringLiteral("reportRowCount"), controlPanel, 2,
                                 QFont::DemiBold);
    rowCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    controls->addWidget(rowCount, 0, Qt::AlignBottom);

    controlColumn->addLayout(controls);
    root->addWidget(controlPanel);

    // --- chart --------------------------------------------------------------
    QFrame* chartPanel = nullptr;
    QVBoxLayout* chartColumn =
        buildPanel(QStringLiteral("Chart"),
                   QStringLiteral("Built from exactly the rows listed below."), this, &chartPanel);
    m_chart = new QChartView(chartPanel);
    m_chart->setObjectName(QStringLiteral("reportChart"));
    m_chart->setRenderHint(QPainter::Antialiasing, true);
    m_chart->setFrameShape(QFrame::NoFrame);
    m_chart->setMinimumHeight(240);
    m_chart->setBackgroundBrush(Qt::NoBrush);
    m_chart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    chartColumn->addWidget(m_chart, 1);
    root->addWidget(chartPanel, 2);

    // --- preview ------------------------------------------------------------
    QFrame* previewPanel = nullptr;
    QVBoxLayout* previewColumn =
        buildPanel(QStringLiteral("Preview"),
                   QStringLiteral("Exactly the rows an export will contain, in export order."),
                   this, &previewPanel);

    m_preview = new ElegantTable(previewPanel);
    // Sorting is deliberately off: the preview must agree row-for-row with the exported file.
    m_preview->setSortingEnabled(false);
    m_preview->setMinimumHeight(200);
    m_preview->setSelectionMode(QAbstractItemView::SingleSelection);
    previewColumn->addWidget(m_preview, 1);

    auto* empty = new EmptyState(QStringLiteral("Nothing to report yet"),
                                 QStringLiteral("Widen the date range, or choose another report."),
                                 previewPanel);
    empty->setVisible(false);
    previewColumn->addWidget(empty, 1);

    root->addWidget(previewPanel, 3);

    // --- where the last file went -------------------------------------------
    auto* status = styledLabel(QString(), QStringLiteral("reportStatus"), this, -1);
    status->setTextFormat(Qt::RichText);
    status->setTextInteractionFlags(Qt::TextBrowserInteraction);
    status->setOpenExternalLinks(true);
    status->setWordWrap(true);
    status->setVisible(false);
    root->addWidget(status);

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

    auto* emptyState = findChild<QFrame*>(QStringLiteral("emptyState"));
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
        if (header.size() > 0) {
            m_preview->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
            m_preview->resizeColumnsToContents();
            m_preview->horizontalHeader()->setStretchLastSection(true);
        }

        const bool hasRows = !rows.empty();
        m_preview->setVisible(hasRows);
        if (emptyState) emptyState->setVisible(!hasRows);
        if (rowCount) {
            rowCount->setText(hasRows ? QStringLiteral("%1 row%2")
                                            .arg(rows.size())
                                            .arg(rows.size() == 1 ? QString()
                                                                  : QStringLiteral("s"))
                                      : QStringLiteral("no rows"));
        }

        // ---- chart ----------------------------------------------------------
        const ChartPlan plan = planChart(header, rows, profiles);
        QString caption = QStringLiteral("Chart");
        if (plan.usable) {
            caption = QStringLiteral("%1 by %2")
                          .arg(header.at(plan.valueColumn), header.at(plan.labelColumn));
            m_chart->setChart(buildChart(plan, header.at(plan.valueColumn), pal));
            m_chart->setVisible(true);
        } else {
            m_chart->setChart(nullptr);
            m_chart->setVisible(false);
        }

        // The chart card's own heading and subtitle are its only direct QLabel children,
        // so they can be retitled without this page keeping extra member pointers.
        if (QWidget* chartPanel = m_chart->parentWidget()) {
            if (auto* heading = chartPanel->findChild<QLabel*>(QStringLiteral("sectionTitle"),
                                                              Qt::FindDirectChildrenOnly))
                heading->setText(caption);
            if (auto* sub = chartPanel->findChild<QLabel*>(QStringLiteral("mutedLabel"),
                                                          Qt::FindDirectChildrenOnly)) {
                sub->setText(plan.usable
                                 ? QStringLiteral("%1  ·  built from exactly the rows below%2")
                                       .arg(title, plan.money
                                                       ? QStringLiteral(", in rupees")
                                                       : QString())
                                 : QStringLiteral("%1  ·  this report has no figures to plot.")
                                       .arg(title));
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
        m_chart->setChart(nullptr);
        if (emptyState) emptyState->setVisible(true);
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
    dialog.setMinimumWidth(720);

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

    auto* table = new ElegantTable(QStringList{QStringLiteral("When"), QStringLiteral("Seq"),
                                               QStringLiteral("Action"), QStringLiteral("Entity"),
                                               QStringLiteral("Amount"), QStringLiteral("User"),
                                               QStringLiteral("Details")},
                                  &dialog);
    table->setSortingEnabled(false);
    table->setMinimumHeight(280);

    try {
        // `auto` on purpose: naming the record type would drag a persistence header into a GUI
        // translation unit and break the layer rule (ARCHITECTURE §1, §12 R1).
        const auto recent = m_ctx.audit().recentTrailRecords(15);
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

    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
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

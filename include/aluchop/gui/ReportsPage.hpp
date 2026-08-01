#pragma once

/**
 * @file ReportsPage.hpp
 * @brief Screen 8 — the five report kinds with chart preview, CSV and PDF export.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QString>

#include "aluchop/gui/Page.hpp"

class QChartView;
class QComboBox;
class QDateEdit;
class QTableWidget;

namespace aluchop::gui {

/**
 * @brief Reporting: pick a kind and a date range, look at it, then export it.
 *
 * The five kinds — Sales, Inventory, Orders, Customers, Employees — are produced by
 * services::ReportService's factory, which returns a `std::unique_ptr<ReportGenerator>`.
 * This screen calls the abstract interface (`title()`, `header()`, `rows()`, `exportCsv()`)
 * and never branches on the report kind after the factory call.
 *
 * Two export paths, deliberately split by layer:
 *  - **CSV** is written by services::ReportGenerator (which drives the CSV writer down in the
 *    persistence layer), keeping file writing out of the GUI.
 *  - **PDF** is rendered by gui::PdfExporter, because QtPrintSupport is a GUI-layer module and
 *    must not leak downwards.
 *
 * "Verify audit" runs services::AuditService's integrity check over the fixed-record binary
 * audit trail — the random-access file layer surfaced as a user-visible feature.
 *
 * @oop-concept Abstract Class (surfaced) :: one screen renders five reports through a single
 *              ReportGenerator interface
 */
class ReportsPage final : public Page {
    Q_OBJECT
public:
    /// @param ctx composition root; uses AppContext::reports() and audit().
    /// @param parent Qt parent.
    explicit ReportsPage(services::AppContext& ctx, QWidget* parent = nullptr);

    /// @return "Reports".
    QString pageTitle() const override;

    /// Rebuilds the current report for the current date range and redraws preview and chart.
    void refresh() override;

private slots:
    /// Report kind changed — rebuild via the report factory.
    void onKindChanged();

    /// Writes the current report to reports/ as CSV via services::ReportGenerator.
    void onExportCsv();

    /// Writes the current report to reports/ as PDF via gui::PdfExporter.
    void onExportPdf();

    /// Verifies the binary audit trail's sequence and checksums and reports the result.
    void onVerifyAudit();

private:
    QComboBox* m_kind = nullptr;      ///< Sales | Inventory | Orders | Customers | Employees.
    QDateEdit* m_from = nullptr;      ///< Range start (inclusive).
    QDateEdit* m_to = nullptr;        ///< Range end (inclusive).
    QChartView* m_chart = nullptr;    ///< Kind-appropriate QtCharts series.
    QTableWidget* m_preview = nullptr;///< Exactly the rows the export will contain.
};

} // namespace aluchop::gui

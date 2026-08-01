#pragma once

/**
 * @file PdfExporter.hpp
 * @brief PDF rendering and receipt printing — the only home of QtPrintSupport.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QString>
#include <QStringList>

#include <vector>

#include "aluchop/core/Result.hpp"
#include "aluchop/models/Bill.hpp"

class QWidget;

namespace aluchop::gui {

/**
 * @brief Stateless utility that turns reports and bills into PDFs (and paper).
 *
 * QtPrintSupport is a presentation-layer module, so it is deliberately confined to this class:
 * services produce data (rows, a models::Bill) and this GUI helper renders it. Nothing below
 * the GUI layer includes QPrinter or QPdfWriter.
 *
 * Every entry point is static and every failure is returned as a `core::Result`, never thrown
 * across the GUI boundary and never reported as a silent no-op — callers show the error text
 * in a toast.
 *
 * @oop-concept Static Members :: rendering needs no object state, so the class is a namespace
 *              with access control rather than a thing to instantiate
 * @oop-concept Class Template (used) :: results travel back in core::Result<QString>
 */
class PdfExporter {
public:
    /// Renders a tabular report to PDF via QTextDocument HTML → QPdfWriter. The page footer
    /// carries the core::kAppInfo credit line required by SPEC §10.
    /// @param title report title printed at the top of every page.
    /// @param header column captions.
    /// @param rows the report body; each inner list must match @p header in size.
    /// @param outPath destination file path (normally under reports/).
    /// @return the written path on success, or an error message.
    static core::Result<QString> exportReportPdf(const QString& title, const QStringList& header,
                                                 const std::vector<QStringList>& rows,
                                                 const QString& outPath);

    /// Renders a settled bill as a narrow receipt PDF, reusing
    /// models::Bill::toPrintableText() so paper, PDF and on-screen receipts cannot diverge.
    /// @param bill the settled bill.
    /// @param outPath destination file path.
    /// @return the written path on success, or an error message.
    static core::Result<QString> receiptPdf(const models::Bill& bill, const QString& outPath);

    /// Shows a QPrintDialog and prints the receipt on the chosen printer.
    /// Cancelling the dialog is not an error and does nothing.
    /// @param bill the settled bill.
    /// @param parent dialog parent for correct modality.
    static void printReceipt(const models::Bill& bill, QWidget* parent);
};

} // namespace aluchop::gui

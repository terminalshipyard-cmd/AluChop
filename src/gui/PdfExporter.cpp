/**
 * @file PdfExporter.cpp
 * @brief PDF rendering and receipt printing — the only home of QtPrintSupport.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * QtPrintSupport is a presentation-layer module, so every `QPdfWriter`, `QPrinter` and
 * `QPrintDialog` in AluChop lives in this one translation unit. Services hand over finished data
 * (a title, a header row, body rows, or a settled models::Bill) and this file decides only how it
 * is laid out on paper.
 *
 * @par Two page formats, deliberately different
 *  - **Reports** are A4 portrait: a branded band, a title block, a measured table with money
 *    columns right-aligned, repeating column captions on continuation pages, and a footer on every
 *    page carrying the SPEC §10 copyright line and a "Page n of m".
 *  - **Receipts** are a narrow 80 mm till roll rendered in a monospaced face, because the receipt
 *    body comes verbatim from models::Bill::toPrintableText() — the very same 46-column text the
 *    on-screen preview and a thermal printer show. Paper, PDF and screen therefore cannot diverge.
 *
 * @par Why the palette is fixed to Light
 * A PDF is printed on white paper. Rendering the Dark theme's deep green-greys would waste toner
 * and read badly, so this file always paints from gui::ThemeManager::kLight — the same const
 * Palette object the Light theme uses, so the brand colours still come from one place and no hex
 * literal appears here.
 */

#include "aluchop/gui/PdfExporter.hpp"

#include <QChar>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLatin1Char>
#include <QLatin1String>
#include <QMarginsF>
#include <QMessageBox>
#include <QPagedPaintDevice>
#include <QPageLayout>
#include <QPageSize>
#include <QPaintDevice>
#include <QPainter>
#include <QPdfWriter>
#include <QPen>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <QPrintDialog>
#include <QPrinter>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "aluchop/core/AppInfo.hpp"
#include "aluchop/gui/ThemeManager.hpp"

namespace aluchop::gui {
namespace {

/// Print resolution in dots per inch. 300 dpi is the usual "document quality" figure and keeps
/// hairlines visible without producing an enormous file.
constexpr int kPdfDpi = 300;

/// Receipt roll width in millimetres — the standard thermal till roll.
constexpr qreal kReceiptWidthMm = 80.0;

/// models::Bill::toPrintableText() lays its receipt out in exactly this many monospaced columns,
/// so the font is scaled until 46 characters fill the printable width.
constexpr int kReceiptColumns = 46;

/// Everything printed lands on white paper, so the printed palette is always the Light one.
const Palette& paperPalette() { return ThemeManager::kLight; }

/// Millimetres → device pixels at @p dpi.
qreal mmToPx(qreal millimetres, int dpi) { return millimetres / 25.4 * static_cast<qreal>(dpi); }

/// Device pixels → millimetres at @p dpi.
qreal pxToMm(qreal pixels, int dpi) { return pixels / static_cast<qreal>(dpi) * 25.4; }

/**
 * @brief Recognises a cell that holds a number or an amount of money.
 * @param raw the cell text exactly as the report produced it.
 * @param[out] money true when the cell carried a currency prefix (`Rs …` / `NPR …`).
 * @return true when the cell is numeric, which is what makes a column right-aligned.
 *
 * core::Money::toString() renders `Rs 1,250.00` (and `-Rs 1,250.00`), so the prefix, the sign and
 * the thousands separators are all stripped before the parse is attempted.
 */
bool isNumericCell(const QString& raw, bool& money) {
    QString t = raw.trimmed();
    money = false;
    if (t.isEmpty()) return false;

    if (t.startsWith(QLatin1Char('-')) || t.startsWith(QLatin1Char('+')))
        t = t.mid(1).trimmed();

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
    (void)t.toDouble(&ok);
    return ok;
}

/**
 * @brief Lays a tabular report out on A4 and paints it page by page.
 *
 * The class exists so the geometry is computed exactly once and then used by both the page-count
 * pass and the painting pass — if the two disagreed, the "Page 2 of 1" footers would give it away.
 *
 * @oop-concept Objects as Members :: the renderer owns its font metrics and column geometry as
 *              value members, so the whole layout is created and destroyed with one object
 */
class ReportRenderer {
public:
    /// @param painter an already-begun painter on @p device.
    /// @param device the paged device being written (a QPdfWriter or a QPrinter).
    /// @param title report title, printed on page one and echoed on continuation pages.
    /// @param header column captions.
    /// @param rows the report body.
    ReportRenderer(QPainter& painter, QPagedPaintDevice& device, QString title, QStringList header,
                   const std::vector<QStringList>& rows)
        : m_p(painter),
          m_dev(device),
          m_pal(paperPalette()),
          m_title(std::move(title)),
          m_header(std::move(header)),
          m_rows(rows) {
        m_dpi = device.logicalDpiX() > 0 ? device.logicalDpiX() : kPdfDpi;
        m_w = static_cast<qreal>(device.width());
        m_h = static_cast<qreal>(device.height());
        buildFonts();
        measure();
    }

    /// Paints every page of the report.
    void render() {
        const int pages = pageCount();
        int page = 1;

        qreal y = drawBrandBand(false);
        y = drawTitleBlock(y);

        if (m_rows.empty()) {
            drawTableHeader(y);
            y += m_headRowH;
            m_p.setFont(m_bodyFont);
            m_p.setPen(m_pal.textMuted);
            m_p.drawText(QRectF(0.0, y, m_w, m_rowH * 3.0), Qt::AlignCenter,
                         QStringLiteral("No rows matched this selection."));
            drawFooter(page, pages);
            return;
        }

        drawTableHeader(y);
        y += m_headRowH;

        bool zebra = false;
        for (const QStringList& row : m_rows) {
            if (y + m_rowH > m_bottom) {
                drawFooter(page, pages);
                m_dev.newPage();
                ++page;
                y = drawBrandBand(true);
                drawTableHeader(y);
                y += m_headRowH;
                zebra = false;
            }
            drawRow(row, y, zebra);
            zebra = !zebra;
            y += m_rowH;
        }

        drawFooter(page, pages);
    }

private:
    // -- geometry helpers ---------------------------------------------------

    qreal mm(qreal v) const { return mmToPx(v, m_dpi); }

    void buildFonts() {
        const QFont base;  // the application's default UI family
        m_brandFont = base;
        m_brandFont.setPointSizeF(19.0);
        m_brandFont.setWeight(QFont::Bold);

        m_brandSubFont = base;
        m_brandSubFont.setPointSizeF(7.5);

        m_titleFont = base;
        m_titleFont.setPointSizeF(14.5);
        m_titleFont.setWeight(QFont::DemiBold);

        m_metaFont = base;
        m_metaFont.setPointSizeF(8.0);

        m_headFont = base;
        m_headFont.setPointSizeF(8.2);
        m_headFont.setWeight(QFont::DemiBold);

        m_bodyFont = base;
        m_bodyFont.setPointSizeF(8.6);

        m_footFont = base;
        m_footFont.setPointSizeF(6.8);
    }

    /// Computes row heights, column widths, column alignment and the page geometry.
    void measure() {
        const QFontMetricsF fmTitle(m_titleFont, &m_dev);
        const QFontMetricsF fmMeta(m_metaFont, &m_dev);
        const QFontMetricsF fmHead(m_headFont, &m_dev);
        const QFontMetricsF fmBody(m_bodyFont, &m_dev);

        m_bandH = mm(17.0);
        m_contBandH = mm(11.0);
        m_footerH = mm(15.0);
        m_bottom = m_h - m_footerH;

        m_rowH = std::max(fmBody.height() * 1.85, mm(6.0));
        m_headRowH = std::max(fmHead.height() * 2.0, mm(6.6));

        m_titleBlockH = fmTitle.height() + mm(1.6) + fmMeta.height() + mm(6.0);
        m_tableTopFirst = m_bandH + mm(9.0) + m_titleBlockH;
        m_tableTopCont = m_contBandH + mm(7.0);

        // --- column widths, measured from the real text ----------------------
        const int cols = m_header.size();
        m_colW.assign(static_cast<std::size_t>(std::max(cols, 0)), 0.0);
        m_colRight.assign(static_cast<std::size_t>(std::max(cols, 0)), false);
        if (cols <= 0) return;

        const qreal pad = mm(2.6);
        std::vector<qreal> want(static_cast<std::size_t>(cols), 0.0);
        for (int c = 0; c < cols; ++c)
            want[static_cast<std::size_t>(c)] = fmHead.horizontalAdvance(m_header.at(c));

        std::vector<int> numeric(static_cast<std::size_t>(cols), 0);
        std::vector<int> monetary(static_cast<std::size_t>(cols), 0);
        std::vector<int> filled(static_cast<std::size_t>(cols), 0);

        const std::size_t sample = std::min<std::size_t>(m_rows.size(), 400u);
        for (std::size_t r = 0; r < sample; ++r) {
            const QStringList& row = m_rows[r];
            for (int c = 0; c < cols && c < row.size(); ++c) {
                const QString& cell = row.at(c);
                const auto uc = static_cast<std::size_t>(c);
                want[uc] = std::max(want[uc], fmBody.horizontalAdvance(cell));
                if (cell.trimmed().isEmpty()) continue;
                ++filled[uc];
                bool money = false;
                if (isNumericCell(cell, money)) {
                    ++numeric[uc];
                    if (money) ++monetary[uc];
                }
            }
        }

        for (int c = 0; c < cols; ++c) {
            const auto uc = static_cast<std::size_t>(c);
            // A column reads as a figure column when most of its filled cells parse as numbers.
            m_colRight[uc] = filled[uc] > 0 && numeric[uc] * 10 >= filled[uc] * 6;
            want[uc] += 2.0 * pad;
            want[uc] = std::max(want[uc], mm(11.0));
        }

        qreal total = 0.0;
        for (qreal v : want) total += v;
        if (total <= 0.0) total = 1.0;

        // No single column may swallow the page: cap the widest and re-normalise once.
        if (cols > 2) {
            const qreal cap = total * 0.42;
            qreal capped = 0.0;
            for (qreal& v : want) {
                v = std::min(v, cap);
                capped += v;
            }
            total = capped > 0.0 ? capped : 1.0;
        }

        const qreal scale = m_w / total;
        for (int c = 0; c < cols; ++c)
            m_colW[static_cast<std::size_t>(c)] = want[static_cast<std::size_t>(c)] * scale;

        m_cellPad = pad;
    }

    /// How many pages the body needs — mirrors the break rule in render() exactly.
    int pageCount() const {
        if (m_rows.empty()) return 1;
        const qreal room1 = m_bottom - m_tableTopFirst - m_headRowH;
        const qreal roomN = m_bottom - m_tableTopCont - m_headRowH;
        const int cap1 = std::max(1, static_cast<int>(std::floor(room1 / m_rowH)));
        const int capN = std::max(1, static_cast<int>(std::floor(roomN / m_rowH)));
        const int n = static_cast<int>(m_rows.size());
        if (n <= cap1) return 1;
        return 1 + (n - cap1 + capN - 1) / capN;
    }

    // -- painting -----------------------------------------------------------

    /// Draws the sage brand band and returns the y coordinate just below it.
    qreal drawBrandBand(bool continuation) {
        const qreal height = continuation ? m_contBandH : m_bandH;
        const QRectF band(0.0, 0.0, m_w, height);

        m_p.setPen(Qt::NoPen);
        m_p.setBrush(m_pal.primary);
        m_p.drawRoundedRect(band, mm(2.4), mm(2.4));
        m_p.setBrush(Qt::NoBrush);

        QColor soft = m_pal.card;
        soft.setAlpha(205);

        const qreal left = mm(7.0);
        const qreal right = mm(7.0);

        if (!continuation) {
            const QFontMetricsF fmBrand(m_brandFont, &m_dev);
            const QFontMetricsF fmSub(m_brandSubFont, &m_dev);
            const qreal blockH = fmBrand.height() + fmSub.height();
            qreal y = (height - blockH) / 2.0;

            m_p.setFont(m_brandFont);
            m_p.setPen(m_pal.card);
            m_p.drawText(QRectF(left, y, m_w - left - right, fmBrand.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("AluChop"));
            y += fmBrand.height();

            m_p.setFont(m_brandSubFont);
            m_p.setPen(soft);
            m_p.drawText(QRectF(left, y, m_w - left - right, fmSub.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Restaurant Management System"));

            m_p.setFont(m_brandSubFont);
            m_p.setPen(soft);
            m_p.drawText(QRectF(left, 0.0, m_w - left - right, height),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("Version %1  ·  %2")
                             .arg(QString::fromUtf8(core::kAppInfo.version),
                                  QDateTime::currentDateTime().toString(
                                      QStringLiteral("d MMM yyyy"))));
        } else {
            m_p.setFont(m_brandSubFont);
            m_p.setPen(m_pal.card);
            m_p.drawText(QRectF(left, 0.0, m_w - left - right, height),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("AluChop  ·  %1").arg(m_title));
            m_p.setPen(soft);
            m_p.drawText(QRectF(left, 0.0, m_w - left - right, height),
                         Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("continued"));
        }

        return continuation ? m_tableTopCont : height + mm(9.0);
    }

    /// Draws the report title and its meta line; returns the table's top y coordinate.
    qreal drawTitleBlock(qreal top) {
        const QFontMetricsF fmTitle(m_titleFont, &m_dev);
        const QFontMetricsF fmMeta(m_metaFont, &m_dev);

        m_p.setFont(m_titleFont);
        m_p.setPen(m_pal.text);
        m_p.drawText(QRectF(0.0, top, m_w, fmTitle.height()), Qt::AlignLeft | Qt::AlignVCenter,
                     m_title);

        const QString meta =
            QStringLiteral("%1 row%2  ·  generated %3")
                .arg(m_rows.size())
                .arg(m_rows.size() == 1 ? QString() : QStringLiteral("s"),
                     QDateTime::currentDateTime().toString(QStringLiteral("d MMMM yyyy, hh:mm")));

        m_p.setFont(m_metaFont);
        m_p.setPen(m_pal.textMuted);
        m_p.drawText(QRectF(0.0, top + fmTitle.height() + mm(1.6), m_w, fmMeta.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, meta);

        return m_tableTopFirst;
    }

    /// Draws the tinted caption row of the table.
    void drawTableHeader(qreal y) {
        QColor tint = m_pal.accent;
        tint.setAlpha(80);
        m_p.setPen(Qt::NoPen);
        m_p.setBrush(tint);
        m_p.drawRoundedRect(QRectF(0.0, y, m_w, m_headRowH), mm(1.4), mm(1.4));
        m_p.setBrush(Qt::NoBrush);

        m_p.setFont(m_headFont);
        m_p.setPen(m_pal.primary);

        qreal x = 0.0;
        for (int c = 0; c < m_header.size() && c < static_cast<int>(m_colW.size()); ++c) {
            const auto uc = static_cast<std::size_t>(c);
            const QRectF cell(x + m_cellPad, y, m_colW[uc] - 2.0 * m_cellPad, m_headRowH);
            const int flags =
                (m_colRight[uc] ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter;
            m_p.drawText(cell, flags, elide(m_header.at(c), m_headFont, cell.width()));
            x += m_colW[uc];
        }

        m_p.setPen(QPen(m_pal.border, std::max(mm(0.18), 1.0)));
        m_p.drawLine(QPointF(0.0, y + m_headRowH), QPointF(m_w, y + m_headRowH));
    }

    /// Draws one body row, optionally on a zebra tint.
    void drawRow(const QStringList& row, qreal y, bool zebra) {
        if (zebra) {
            QColor wash = m_pal.background;
            wash.setAlpha(170);
            m_p.setPen(Qt::NoPen);
            m_p.setBrush(wash);
            m_p.drawRect(QRectF(0.0, y, m_w, m_rowH));
            m_p.setBrush(Qt::NoBrush);
        }

        m_p.setFont(m_bodyFont);
        qreal x = 0.0;
        for (int c = 0; c < static_cast<int>(m_colW.size()); ++c) {
            const auto uc = static_cast<std::size_t>(c);
            const QString text = c < row.size() ? row.at(c) : QString();
            const QRectF cell(x + m_cellPad, y, m_colW[uc] - 2.0 * m_cellPad, m_rowH);
            const int flags =
                (m_colRight[uc] ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter;
            m_p.setPen(c == 0 ? m_pal.text : (m_colRight[uc] ? m_pal.text : m_pal.textMuted));
            m_p.drawText(cell, flags, elide(text, m_bodyFont, cell.width()));
            x += m_colW[uc];
        }

        QColor hair = m_pal.border;
        hair.setAlpha(120);
        m_p.setPen(QPen(hair, std::max(mm(0.12), 1.0)));
        m_p.drawLine(QPointF(0.0, y + m_rowH), QPointF(m_w, y + m_rowH));
    }

    /// Draws the SPEC §10 copyright line and the page counter at the bottom of every page.
    void drawFooter(int page, int pages) {
        const qreal top = m_bottom + mm(3.0);

        m_p.setPen(QPen(m_pal.border, std::max(mm(0.18), 1.0)));
        m_p.drawLine(QPointF(0.0, top), QPointF(m_w, top));

        const QFontMetricsF fmFoot(m_footFont, &m_dev);
        const qreal textTop = top + mm(2.2);
        const qreal creditW = m_w * 0.74;

        m_p.setFont(m_footFont);
        m_p.setPen(m_pal.textMuted);
        m_p.drawText(QRectF(0.0, textTop, creditW, m_footerH - mm(3.0)),
                     Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                     QString::fromUtf8(core::kCopyrightNotice));

        m_p.setPen(m_pal.primary);
        m_p.drawText(QRectF(creditW, textTop, m_w - creditW, fmFoot.height() * 2.0),
                     Qt::AlignRight | Qt::AlignTop,
                     QStringLiteral("Page %1 of %2").arg(page).arg(pages));
    }

    /// Shortens @p text with an ellipsis so it never spills into the next column.
    QString elide(const QString& text, const QFont& font, qreal width) {
        const QFontMetricsF fm(font, &m_dev);
        return fm.elidedText(text, Qt::ElideRight, std::max(width, 1.0));
    }

    QPainter& m_p;
    QPagedPaintDevice& m_dev;
    const Palette& m_pal;

    QString m_title;
    QStringList m_header;
    const std::vector<QStringList>& m_rows;

    int m_dpi = kPdfDpi;
    qreal m_w = 0.0;
    qreal m_h = 0.0;

    QFont m_brandFont, m_brandSubFont, m_titleFont, m_metaFont, m_headFont, m_bodyFont, m_footFont;

    qreal m_bandH = 0.0;
    qreal m_contBandH = 0.0;
    qreal m_footerH = 0.0;
    qreal m_bottom = 0.0;
    qreal m_titleBlockH = 0.0;
    qreal m_tableTopFirst = 0.0;
    qreal m_tableTopCont = 0.0;
    qreal m_rowH = 0.0;
    qreal m_headRowH = 0.0;
    qreal m_cellPad = 0.0;

    std::vector<qreal> m_colW;   ///< resolved pixel width of every column
    std::vector<bool> m_colRight;///< true where the column holds figures and is right-aligned
};

/**
 * @brief Lays a till receipt out on a narrow roll.
 *
 * The body is models::Bill::toPrintableText() rendered verbatim in a fixed-pitch face, which is
 * what guarantees the PDF, the printed slip and the on-screen preview are the same document.
 */
class ReceiptRenderer {
public:
    /// @param device the paged device the receipt will be painted on (used for font metrics).
    /// @param bill the bill to render.
    explicit ReceiptRenderer(QPagedPaintDevice& device, const models::Bill& bill)
        : m_dev(device), m_pal(paperPalette()), m_body(bill.toPrintableText().split(QLatin1Char('\n'))) {
        m_dpi = device.logicalDpiX() > 0 ? device.logicalDpiX() : kPdfDpi;
        m_w = static_cast<qreal>(device.width());
        buildFonts();
        measure();
    }

    /// @return the height in device pixels the whole receipt needs.
    qreal heightPx() const { return m_totalH; }

    /// @return the height in millimetres the whole receipt needs.
    qreal heightMm() const { return pxToMm(m_totalH, m_dpi); }

    /// Paints the receipt, breaking to a new page only if the device runs out of room.
    void paint(QPainter& p) {
        const qreal pageH = static_cast<qreal>(m_dev.height());
        qreal y = 0.0;

        const QFontMetricsF fmBrand(m_brandFont, &m_dev);
        const QFontMetricsF fmSub(m_subFont, &m_dev);
        const QFontMetricsF fmMono(m_monoFont, &m_dev);
        const QFontMetricsF fmFoot(m_footFont, &m_dev);

        p.setFont(m_brandFont);
        p.setPen(m_pal.primary);
        p.drawText(QRectF(0.0, y, m_w, fmBrand.height()), Qt::AlignHCenter | Qt::AlignVCenter,
                   QStringLiteral("AluChop"));
        y += fmBrand.height();

        p.setFont(m_subFont);
        p.setPen(m_pal.textMuted);
        p.drawText(QRectF(0.0, y, m_w, fmSub.height()), Qt::AlignHCenter | Qt::AlignVCenter,
                   QStringLiteral("Restaurant Management System"));
        y += fmSub.height() + mm(2.0);

        p.setPen(QPen(m_pal.border, std::max(mm(0.2), 1.0)));
        p.drawLine(QPointF(0.0, y), QPointF(m_w, y));
        y += mm(2.6);

        p.setFont(m_monoFont);
        p.setPen(m_pal.text);
        const qreal lineH = fmMono.lineSpacing();
        for (const QString& line : m_body) {
            if (y + lineH > pageH) {
                m_dev.newPage();
                y = mm(3.0);
                p.setFont(m_monoFont);
                p.setPen(m_pal.text);
            }
            p.drawText(QRectF(0.0, y, m_w, lineH), Qt::AlignLeft | Qt::AlignVCenter, line);
            y += lineH;
        }

        y += mm(2.4);
        p.setPen(QPen(m_pal.border, std::max(mm(0.2), 1.0)));
        p.drawLine(QPointF(0.0, y), QPointF(m_w, y));
        y += mm(2.0);

        p.setFont(m_footFont);
        p.setPen(m_pal.textMuted);
        p.drawText(QRectF(0.0, y, m_w, std::max(m_footH, fmFoot.height())),
                   Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                   QString::fromUtf8(core::kCopyrightNotice));
    }

private:
    qreal mm(qreal v) const { return mmToPx(v, m_dpi); }

    void buildFonts() {
        const QFont base;

        m_brandFont = base;
        m_brandFont.setPointSizeF(15.0);
        m_brandFont.setWeight(QFont::Bold);

        m_subFont = base;
        m_subFont.setPointSizeF(6.4);

        m_footFont = base;
        m_footFont.setPointSizeF(5.4);

        /// @oop-concept Static Members (used) :: QFontDatabase's system-font lookup is a static
        /// service — a fixed-pitch face is what keeps the 46-column receipt columns aligned.
        m_monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        m_monoFont.setPointSizeF(9.0);

        // Scale the monospaced face until exactly kReceiptColumns characters fill the roll.
        const QFontMetricsF probe(m_monoFont, &m_dev);
        const qreal have = probe.horizontalAdvance(QString(kReceiptColumns, QLatin1Char('0')));
        if (have > 0.0 && m_w > 0.0) {
            const qreal scaled = 9.0 * (m_w / have);
            m_monoFont.setPointSizeF(std::min(std::max(scaled, 3.5), 14.0));
        }
    }

    void measure() {
        const QFontMetricsF fmBrand(m_brandFont, &m_dev);
        const QFontMetricsF fmSub(m_subFont, &m_dev);
        const QFontMetricsF fmMono(m_monoFont, &m_dev);
        const QFontMetricsF fmFoot(m_footFont, &m_dev);

        m_footH = fmFoot
                      .boundingRect(QRectF(0.0, 0.0, std::max(m_w, 1.0), mm(60.0)),
                                    Qt::AlignHCenter | Qt::TextWordWrap,
                                    QString::fromUtf8(core::kCopyrightNotice))
                      .height();

        m_totalH = fmBrand.height() + fmSub.height() + mm(2.0)      // brand block
                   + mm(2.6)                                        // rule + gap
                   + fmMono.lineSpacing() * static_cast<qreal>(m_body.size())
                   + mm(2.4) + mm(2.0)                              // rule + gap
                   + m_footH;
    }

    QPagedPaintDevice& m_dev;
    const Palette& m_pal;
    QStringList m_body;

    int m_dpi = kPdfDpi;
    qreal m_w = 0.0;
    qreal m_totalH = 0.0;
    qreal m_footH = 0.0;

    QFont m_brandFont, m_subFont, m_monoFont, m_footFont;
};

/// Creates the directory @p outPath lives in so the writer cannot fail on a missing folder.
bool ensureParentDirectory(const QString& outPath) {
    const QFileInfo info(outPath);
    const QString dir = info.absolutePath();
    if (dir.isEmpty()) return false;
    return QDir(dir).exists() || QDir().mkpath(dir);
}

} // namespace

// ---------------------------------------------------------------------------
// Report PDF
// ---------------------------------------------------------------------------

core::Result<QString> PdfExporter::exportReportPdf(const QString& title, const QStringList& header,
                                                   const std::vector<QStringList>& rows,
                                                   const QString& outPath) {
    if (outPath.trimmed().isEmpty())
        return core::Result<QString>::err(QStringLiteral("No destination file was given."));
    if (header.isEmpty())
        return core::Result<QString>::err(
            QStringLiteral("A report needs at least one column to be exported."));
    if (!ensureParentDirectory(outPath))
        return core::Result<QString>::err(
            QStringLiteral("Could not create the folder for %1.").arg(outPath));

    QPdfWriter writer(outPath);
    writer.setResolution(kPdfDpi);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Portrait);
    writer.setPageMargins(QMarginsF(15.0, 13.0, 15.0, 13.0), QPageLayout::Millimeter);
    writer.setTitle(title);
    writer.setCreator(QString::fromUtf8(core::kAppInfo.appName));

    if (writer.width() <= 0 || writer.height() <= 0)
        return core::Result<QString>::err(
            QStringLiteral("The page layout left no printable area for %1.").arg(title));

    QPainter painter;
    if (!painter.begin(&writer))
        return core::Result<QString>::err(
            QStringLiteral("Could not open %1 for writing — check the folder permissions.")
                .arg(outPath));

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    ReportRenderer renderer(painter, writer, title, header, rows);
    renderer.render();
    painter.end();

    const QFileInfo written(outPath);
    if (!written.exists() || written.size() == 0)
        return core::Result<QString>::err(
            QStringLiteral("%1 was not written — the file is missing or empty.").arg(outPath));

    /// @oop-concept Class Template (used) :: the written path travels home in core::Result<QString>
    return core::Result<QString>::ok(written.absoluteFilePath());
}

// ---------------------------------------------------------------------------
// Receipt PDF
// ---------------------------------------------------------------------------

core::Result<QString> PdfExporter::receiptPdf(const models::Bill& bill, const QString& outPath) {
    if (outPath.trimmed().isEmpty())
        return core::Result<QString>::err(QStringLiteral("No destination file was given."));
    if (!ensureParentDirectory(outPath))
        return core::Result<QString>::err(
            QStringLiteral("Could not create the folder for %1.").arg(outPath));

    QPdfWriter writer(outPath);
    writer.setResolution(kPdfDpi);
    writer.setPageMargins(QMarginsF(5.0, 6.0, 5.0, 7.0), QPageLayout::Millimeter);
    writer.setTitle(bill.orderNumber().isEmpty()
                        ? QStringLiteral("AluChop receipt")
                        : QStringLiteral("AluChop receipt %1").arg(bill.orderNumber()));
    writer.setCreator(QString::fromUtf8(core::kAppInfo.appName));

    // A provisional roll so the printable width is known while the content is measured.
    writer.setPageSize(QPageSize(QSizeF(kReceiptWidthMm, 200.0), QPageSize::Millimeter,
                                 QStringLiteral("AluChopReceipt"), QPageSize::ExactMatch));
    if (writer.width() <= 0)
        return core::Result<QString>::err(
            QStringLiteral("The receipt roll left no printable width."));

    ReceiptRenderer measured(writer, bill);

    // Now grow the page to exactly the height the receipt needs (plus the vertical margins).
    const qreal rollHeightMm = std::max(120.0, measured.heightMm() + 6.0 + 7.0 + 3.0);
    writer.setPageSize(QPageSize(QSizeF(kReceiptWidthMm, rollHeightMm), QPageSize::Millimeter,
                                 QStringLiteral("AluChopReceipt"), QPageSize::ExactMatch));

    QPainter painter;
    if (!painter.begin(&writer))
        return core::Result<QString>::err(
            QStringLiteral("Could not open %1 for writing — check the folder permissions.")
                .arg(outPath));

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    ReceiptRenderer renderer(writer, bill);
    renderer.paint(painter);
    painter.end();

    const QFileInfo written(outPath);
    if (!written.exists() || written.size() == 0)
        return core::Result<QString>::err(
            QStringLiteral("%1 was not written — the file is missing or empty.").arg(outPath));

    return core::Result<QString>::ok(written.absoluteFilePath());
}

// ---------------------------------------------------------------------------
// Paper printing
// ---------------------------------------------------------------------------

void PdfExporter::printReceipt(const models::Bill& bill, QWidget* parent) {
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QSizeF(kReceiptWidthMm, 200.0), QPageSize::Millimeter,
                                  QStringLiteral("AluChopReceipt"), QPageSize::ExactMatch));
    printer.setPageOrientation(QPageLayout::Portrait);
    printer.setPageMargins(QMarginsF(5.0, 6.0, 5.0, 7.0), QPageLayout::Millimeter);
    printer.setDocName(bill.orderNumber().isEmpty()
                           ? QStringLiteral("AluChop receipt")
                           : QStringLiteral("AluChop receipt %1").arg(bill.orderNumber()));

    QPrintDialog dialog(&printer, parent);
    dialog.setWindowTitle(QStringLiteral("Print receipt"));

    // Cancelling is a normal outcome, not a failure — nothing happens and nothing is reported.
    if (dialog.exec() != QDialog::Accepted)
        return;

    QPainter painter;
    if (!painter.begin(&printer)) {
        QMessageBox::warning(parent, QStringLiteral("Could not print"),
                             QStringLiteral("The printer did not accept the job. Try exporting "
                                            "the receipt as a PDF instead."));
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    ReceiptRenderer renderer(printer, bill);
    renderer.paint(painter);
    painter.end();
}

} // namespace aluchop::gui

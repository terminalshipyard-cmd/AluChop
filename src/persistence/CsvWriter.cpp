/**
 * @file CsvWriter.cpp
 * @brief Sequential ASCII (CSV) export — the plain-text half of the raw file layer (SPEC §5.6).
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The counterpart of BinaryRecordFile: where that class seeks freely through fixed-size binary
 * records, this one is strictly sequential and human-readable. Rows are appended in the order they
 * are produced, the file is never seeked, and the stream state is verified after every single row
 * so a disk that fills up halfway through an export is reported instead of silently truncating it.
 *
 * Quoting follows RFC 4180: a cell is wrapped in double quotes when it contains a comma, a double
 * quote, a carriage return or a line feed, and embedded double quotes are doubled.
 */

#include "aluchop/persistence/CsvWriter.hpp"

#include <cerrno>
#include <ios>

#include <QDir>
#include <QFileInfo>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::persistence {
namespace {

/// RFC 4180 line terminator; Excel, Numbers and LibreOffice all read it, and so does every parser.
const QString kRowTerminator = QStringLiteral("\r\n");

/// @brief Raises a FileIOException naming the failing CSV operation, the file and errno.
[[noreturn]] void failIo(const QString& operation, const QString& path)
{
    const QString message = QStringLiteral("csv export: ") + operation + QStringLiteral(" failed");
    throw core::FileIOException(message.toStdString(), path.toStdString(), errno);
}

} // namespace

CsvWriter::~CsvWriter()
{
    // RAII: whatever has been written so far still reaches the disk, and no exception escapes a
    // destructor even when the final flush fails.
    if (m_out.is_open()) {
        m_out.flush();
        m_out.close();
    }
}

void CsvWriter::open(const QString& path)
{
    if (m_out.is_open()) {
        close(); // finish the previous export properly before starting another one
    }
    if (path.trimmed().isEmpty()) {
        throw core::FileIOException("csv export: no path was given", "CsvWriter::open");
    }

    const QFileInfo info(path);
    const QDir parent = info.absoluteDir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        throw core::FileIOException("csv export: cannot create the output directory",
                                    parent.absolutePath().toStdString(), errno);
    }

    m_out.clear();
    m_out.open(info.absoluteFilePath().toStdString(), std::ios::out | std::ios::trunc);
    if (!m_out.is_open() || !m_out.good()) {
        failIo(QStringLiteral("open"), info.absoluteFilePath());
    }

    m_path = info.absoluteFilePath();
    m_rows = 0;
}

void CsvWriter::writeRow(const QStringList& cells)
{
    if (!m_out.is_open()) {
        throw core::FileIOException("csv export: the file is not open", m_path.toStdString());
    }

    QString line;
    for (int i = 0; i < cells.size(); ++i) {
        if (i > 0) {
            line += QLatin1Char(',');
        }
        line += escapeCell(cells.at(i));
    }
    line += kRowTerminator;

    /// @oop-concept File Handling (Error Checking) :: the stream is interrogated after the write,
    /// so a full disk or a read-only volume surfaces as an exception on the very row that failed.
    const QByteArray utf8 = line.toUtf8();
    m_out.write(utf8.constData(), static_cast<std::streamsize>(utf8.size()));
    if (!m_out.good()) {
        failIo(QStringLiteral("write row ") + QString::number(m_rows + 1), m_path);
    }
    ++m_rows;
}

void CsvWriter::close()
{
    if (!m_out.is_open()) {
        return;
    }
    m_out.flush();
    const bool flushed = m_out.good();
    m_out.close();
    const bool closed = !m_out.fail();
    m_out.clear();

    if (!flushed) {
        failIo(QStringLiteral("flush-on-close"), m_path);
    }
    if (!closed) {
        failIo(QStringLiteral("close"), m_path);
    }
}

bool CsvWriter::isOpen() const
{
    return m_out.is_open();
}

QString CsvWriter::escapeCell(const QString& cell)
{
    const bool needsQuotes = cell.contains(QLatin1Char(',')) || cell.contains(QLatin1Char('"')) ||
                             cell.contains(QLatin1Char('\n')) || cell.contains(QLatin1Char('\r'));
    if (!needsQuotes) {
        return cell;
    }

    QString escaped = cell;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\"")); // RFC 4180: a quote is doubled
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

} // namespace aluchop::persistence

#pragma once

/**
 * @file CsvWriter.hpp
 * @brief Sequential ASCII (CSV) output stream — the plain-text half of the file layer.
 *
 * Where @ref BinaryRecordFile is random-access and binary, this class is strictly sequential and
 * human-readable: rows are appended in order and the file is never seeked. It backs every CSV
 * export the Reports screen offers.
 */

#include <fstream>

#include <QString>
#include <QStringList>

namespace aluchop::persistence {

/**
 * @brief Writes RFC-4180-quoted rows to a text file, checking the stream after every operation.
 *
 * /// @oop-concept File Handling (Sequential ASCII) :: plain std::ofstream CSV with per-write checks
 */
class CsvWriter {
public:
    CsvWriter() = default;

    /// @oop-concept Destructor :: an unclosed export still ends up flushed on disk (RAII); never throws
    ~CsvWriter();

    /// An open output file is a unique resource — two writers on one path would interleave rows.
    CsvWriter(const CsvWriter&) = delete;
    CsvWriter& operator=(const CsvWriter&) = delete;

    /**
     * @brief Opens @p path for writing, truncating any previous content and creating parent dirs.
     * @throws core::FileIOException when the directory cannot be created or the file cannot be opened.
     */
    void open(const QString& path);

    /**
     * @brief Escapes and writes one row, then verifies the stream is still good.
     * @throws core::FileIOException when the write failed (disk full, read-only volume, ...).
     * /// @oop-concept File Handling (Error Checking) :: the stream state is tested after every row
     */
    void writeRow(const QStringList& cells);

    /**
     * @brief Flushes and closes the file.
     * @throws core::FileIOException when the final flush failed — a truncated export must be visible.
     */
    void close();

    /// @return true while the file is open.
    bool isOpen() const;

    /// @return how many rows have been written since open() (header row included).
    int rowsWritten() const noexcept { return m_rows; }

    /// @brief RFC-4180 cell quoting: doubles embedded quotes, quotes cells containing `,`, `"` or newlines.
    static QString escapeCell(const QString& cell);

protected:
    /// Protected, not private: `services::ReportGenerator` derives from this class and its own
    /// subclasses drive the writer directly (docs/ARCHITECTURE.md §4).
    std::ofstream m_out;
    QString m_path;
    int m_rows = 0;
};

} // namespace aluchop::persistence

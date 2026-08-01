/**
 * @file BinaryRecordFile.cpp
 * @brief Fixed-record binary file with real random access (SPEC §5.6, docs/ARCHITECTURE.md §7).
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Every record is exactly `sizeof(AuditRecord)` == 128 bytes, so record @e n lives at byte offset
 * `n * 128`. That single fact is what turns this file into a random-access store: no index, no
 * scanning — one `seekg`/`seekp` reaches any record in the file.
 *
 * The binding rule of this translation unit: **no stream operation goes unchecked.** After every
 * `open`, `seekg`, `seekp`, `read`, `write` and `flush` the stream state is tested and a failure
 * becomes a `core::FileIOException` naming the path, the operation and `errno`. The destructor is
 * the one exception — it closes quietly, because throwing during stack unwinding terminates the
 * program.
 */

#include "aluchop/persistence/BinaryRecordFile.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ios>
#include <string>
#include <utility>

#include <QByteArray>
#include <QDir>
#include <QFileInfo>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::persistence {
namespace {

/// Byte offset of the trailing checksum — everything before it is what the checksum covers.
constexpr std::size_t kChecksumOffset = offsetof(AuditRecord, checksum);

static_assert(kChecksumOffset == 124, "checksum must sit at offset 124 of the 128-byte record");
static_assert(sizeof(AuditRecord) % 8 == 0, "record size must stay 8-byte aligned (zero padding)");

/// @brief Raises a FileIOException that names the failing operation, the file and errno.
[[noreturn]] void failIo(const QString& operation, const QString& path, const QString& extra = QString())
{
    QString message = QStringLiteral("audit record file: ") + operation + QStringLiteral(" failed");
    if (!extra.isEmpty()) {
        message += QStringLiteral(" (") + extra + QLatin1Char(')');
    }
    throw core::FileIOException(message.toStdString(), path.toStdString(), errno);
}

} // namespace

BinaryRecordFile::BinaryRecordFile(QString path) : m_path(std::move(path)) {}

BinaryRecordFile::~BinaryRecordFile()
{
    // RAII: release the handle deterministically, and never let an I/O failure escape a destructor.
    if (m_stream.is_open()) {
        m_stream.flush();
        m_stream.close();
    }
}

void BinaryRecordFile::openOrCreate()
{
    if (m_stream.is_open()) {
        return;
    }
    if (m_path.trimmed().isEmpty()) {
        throw core::FileIOException("audit record file: no path was configured", "BinaryRecordFile");
    }

    const QFileInfo info(m_path);
    const QDir parent = info.absoluteDir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        throw core::FileIOException("audit record file: cannot create the parent directory",
                                    parent.absolutePath().toStdString(), errno);
    }

    const std::string native = info.absoluteFilePath().toStdString();

    // in|out|binary is what allows one handle to append at the end AND rewrite a record in the
    // middle. It refuses to create a missing file, so an absent file is created first.
    m_stream.open(native, std::ios::in | std::ios::out | std::ios::binary);
    if (!m_stream.is_open()) {
        m_stream.clear();
        std::ofstream creator(native, std::ios::out | std::ios::binary);
        if (!creator.is_open()) {
            failIo(QStringLiteral("create"), m_path);
        }
        creator.close();
        if (creator.fail()) {
            failIo(QStringLiteral("create/close"), m_path);
        }
        m_stream.open(native, std::ios::in | std::ios::out | std::ios::binary);
        if (!m_stream.is_open()) {
            failIo(QStringLiteral("open"), m_path);
        }
    }
    m_stream.clear();
}

void BinaryRecordFile::close()
{
    if (!m_stream.is_open()) {
        return;
    }
    m_stream.flush();
    const bool flushed = m_stream.good();
    m_stream.close();
    const bool closed = !m_stream.fail();
    m_stream.clear();
    if (!flushed) {
        failIo(QStringLiteral("flush-on-close"), m_path);
    }
    if (!closed) {
        failIo(QStringLiteral("close"), m_path);
    }
}

bool BinaryRecordFile::isOpen() const
{
    return m_stream.is_open();
}

void BinaryRecordFile::ensureOpen()
{
    if (!m_stream.is_open()) {
        throw core::FileIOException("audit record file is not open", m_path.toStdString());
    }
    // A previous read that hit end-of-file leaves eofbit set; clearing here keeps every seek honest.
    m_stream.clear();
}

std::size_t BinaryRecordFile::recordCount()
{
    ensureOpen();

    m_stream.seekg(0, std::ios::end);
    if (m_stream.fail()) {
        failIo(QStringLiteral("seekg(end)"), m_path);
    }
    const std::streamoff bytes = static_cast<std::streamoff>(m_stream.tellg());
    if (bytes < 0 || m_stream.fail()) {
        failIo(QStringLiteral("tellg"), m_path);
    }
    return static_cast<std::size_t>(bytes) / sizeof(AuditRecord);
}

void BinaryRecordFile::append(const AuditRecord& rec)
{
    ensureOpen();

    /// @oop-concept File Handling (Write/Append) :: position at end, write the raw bytes, flush —
    /// and test the stream after each of the three steps.
    m_stream.seekp(0, std::ios::end);
    if (m_stream.fail()) {
        failIo(QStringLiteral("seekp(end)"), m_path);
    }
    m_stream.write(reinterpret_cast<const char*>(&rec), static_cast<std::streamsize>(sizeof(AuditRecord)));
    if (!m_stream.good()) {
        failIo(QStringLiteral("write"), m_path);
    }
    m_stream.flush();
    if (!m_stream.good()) {
        failIo(QStringLiteral("flush"), m_path);
    }
}

AuditRecord BinaryRecordFile::readAt(std::size_t index)
{
    ensureOpen();

    const std::size_t total = recordCount();
    if (index >= total) {
        throw core::FileIOException(
            "audit record index " + std::to_string(index) + " is out of range (" +
                std::to_string(total) + " records)",
            m_path.toStdString());
    }

    /// @oop-concept Random Access File IO :: the get pointer jumps straight to index * 128 bytes;
    /// nothing before the wanted record is ever read.
    m_stream.clear();
    m_stream.seekg(static_cast<std::streamoff>(index * sizeof(AuditRecord)), std::ios::beg);
    if (m_stream.fail()) {
        failIo(QStringLiteral("seekg(record)"), m_path, QStringLiteral("index %1").arg(index));
    }

    AuditRecord rec{};
    m_stream.read(reinterpret_cast<char*>(&rec), static_cast<std::streamsize>(sizeof(AuditRecord)));
    if (!m_stream.good() || m_stream.gcount() != static_cast<std::streamsize>(sizeof(AuditRecord))) {
        m_stream.clear();
        failIo(QStringLiteral("read"), m_path, QStringLiteral("index %1").arg(index));
    }

    if (rec.magic != kAuditRecordMagic) {
        throw core::FileIOException(
            "audit record " + std::to_string(index) + " has a corrupt signature",
            m_path.toStdString());
    }
    if (rec.checksum != checksumOf(rec)) {
        throw core::FileIOException(
            "audit record " + std::to_string(index) + " fails its checksum — the trail was tampered with",
            m_path.toStdString());
    }
    return rec;
}

void BinaryRecordFile::overwriteAt(std::size_t index, const AuditRecord& rec)
{
    ensureOpen();

    const std::size_t total = recordCount();
    if (index >= total) {
        throw core::FileIOException(
            "cannot overwrite audit record " + std::to_string(index) + ": only " +
                std::to_string(total) + " records exist",
            m_path.toStdString());
    }

    /// @oop-concept Random Access File IO :: the put pointer is moved to an arbitrary slot and the
    /// record is replaced in place, so the file length never changes.
    m_stream.clear();
    m_stream.seekp(static_cast<std::streamoff>(index * sizeof(AuditRecord)), std::ios::beg);
    if (m_stream.fail()) {
        failIo(QStringLiteral("seekp(record)"), m_path, QStringLiteral("index %1").arg(index));
    }
    m_stream.write(reinterpret_cast<const char*>(&rec), static_cast<std::streamsize>(sizeof(AuditRecord)));
    if (!m_stream.good()) {
        failIo(QStringLiteral("write"), m_path, QStringLiteral("index %1").arg(index));
    }
    m_stream.flush();
    if (!m_stream.good()) {
        failIo(QStringLiteral("flush"), m_path, QStringLiteral("index %1").arg(index));
    }
}

std::uint32_t BinaryRecordFile::checksumOf(const AuditRecord& rec)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(&rec);
    std::uint32_t sum = 0;
    // Additive sum over bytes [0,124) — exactly the on-disk format fixed by ARCHITECTURE §7.
    // The checksum field itself is excluded, which is why it can be written afterwards.
    for (std::size_t i = 0; i < kChecksumOffset; ++i) {
        sum += static_cast<std::uint32_t>(bytes[i]);
    }
    return sum;
}

void BinaryRecordFile::fillString(char* dest, std::size_t cap, const QString& src)
{
    if (dest == nullptr || cap == 0) {
        return;
    }
    std::memset(dest, 0, cap); // NUL padding is part of the on-disk format, not an afterthought
    const QByteArray ascii = src.toLatin1();
    const std::size_t copied =
        std::min(static_cast<std::size_t>(ascii.size()), cap - 1); // always leave a terminator
    if (copied > 0) {
        std::memcpy(dest, ascii.constData(), copied);
    }
}

} // namespace aluchop::persistence

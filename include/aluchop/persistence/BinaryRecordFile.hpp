#pragma once

/**
 * @file BinaryRecordFile.hpp
 * @brief Fixed-record binary file with true random access — the raw `<fstream>` layer.
 *
 * This is not a demonstration class: it is the storage engine behind AluChop's audit trail
 * (`<dataDir>/audit.bin`). Because every record occupies exactly 128 bytes, record @e n starts at
 * byte offset `n * 128`, so any record can be read or rewritten by a single `seekg`/`seekp` — no
 * scanning, no index. SQLite deliberately plays no part here; the trail is a second, independent
 * medium so that a tampered database can be detected against it.
 *
 * Error-checking contract (docs/ARCHITECTURE.md §3.3, binding): **every** `open`, `seekg`, `seekp`,
 * `read`, `write` and `flush` is followed by a stream-state test, and any failure throws
 * `core::FileIOException` naming the path and the operation. Destructors close quietly.
 */

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <type_traits>

#include <QString>

namespace aluchop::persistence {

/**
 * @brief One 128-byte on-disk audit record.
 *
 * Field order is chosen so the struct has zero padding on any sane ABI: the two 8-byte integers
 * come first, then the 4-byte integers, then the char arrays, then the trailing checksum. Both
 * facts are asserted at compile time below, because a padded record would silently corrupt every
 * offset computation in this class.
 *
 * /// @oop-concept Structures :: a pure-data aggregate with no invariants, written byte-for-byte
 * /// @oop-concept Arrays :: fixed char[] fields are what make the record a constant size
 * /// @oop-concept Binary File Handling :: trivially-copyable POD written with raw read/write
 */
struct AuditRecord {
    std::int64_t  timestampUtcMs;   ///< offset   0, 8 bytes — ms since the Unix epoch, UTC
    std::int64_t  amountPaisa;      ///< offset   8, 8 bytes — money involved, 0 when not monetary
    std::uint32_t magic;            ///< offset  16, 4 bytes — 0x414C4348 ('A''L''C''H')
    std::uint32_t seq;              ///< offset  20, 4 bytes — 1-based, strictly increasing
    std::uint32_t userId;           ///< offset  24, 4 bytes — acting user, 0 = system
    char          action[16];       ///< offset  28 — NUL-padded ASCII, e.g. "ORDER_PAID"
    char          entity[16];       ///< offset  44 — NUL-padded ASCII, e.g. "order:42"
    char          details[64];      ///< offset  60 — NUL-padded ASCII free text, truncated
    std::uint32_t checksum;         ///< offset 124 — additive checksum of bytes [0,124)
};

static_assert(sizeof(AuditRecord) == 128, "AuditRecord must be exactly 128 bytes");
static_assert(std::is_trivially_copyable_v<AuditRecord>, "AuditRecord must be a POD");

/// The on-disk record signature, checked on every read.
inline constexpr std::uint32_t kAuditRecordMagic = 0x414C4348u;

/**
 * @brief Random-access reader/writer over a file of @ref AuditRecord values.
 *
 * The stream is opened `in | out | binary`, which is what allows the same handle to append at the
 * end and rewrite an arbitrary record in the middle.
 */
class BinaryRecordFile {
public:
    /// @param path absolute path of the record file; the file is not touched until openOrCreate().
    explicit BinaryRecordFile(QString path);

    /// @oop-concept Destructor :: the file handle is released deterministically (RAII); never throws
    ~BinaryRecordFile();

    /// A file handle is a unique resource — copying one would give two owners of one position.
    BinaryRecordFile(const BinaryRecordFile&) = delete;
    BinaryRecordFile& operator=(const BinaryRecordFile&) = delete;

    /**
     * @brief Opens the file, creating an empty one first when it does not exist.
     * @throws core::FileIOException when the file cannot be created or opened.
     */
    void openOrCreate();

    /// @brief Flushes and closes the stream; safe to call when already closed.
    void close();

    /// @return true while the stream is open.
    bool isOpen() const;

    /**
     * @brief Number of records currently stored.
     * @details `seekg(0, std::ios::end)` then `tellg() / sizeof(AuditRecord)`.
     * @throws core::FileIOException when the seek or tell fails.
     * @note Non-const because probing the size necessarily moves the get pointer.
     */
    std::size_t recordCount();

    /**
     * @brief Appends one record at the end of the file and flushes it to disk.
     * @throws core::FileIOException on any failed seek, write or flush.
     *
     * /// @oop-concept File Handling (Write/Append) :: seekp(0, end) + raw write + flush, each checked
     */
    void append(const AuditRecord& rec);

    /**
     * @brief Reads the record at @p index.
     * @throws core::FileIOException when @p index is out of range, the seek/read fails, or the
     *         record's magic or checksum does not verify (i.e. the file has been tampered with).
     *
     * /// @oop-concept Random Access File IO :: seekg(index * sizeof(AuditRecord)) positions directly
     */
    AuditRecord readAt(std::size_t index);

    /**
     * @brief Overwrites the record at @p index in place, leaving the file length unchanged.
     * @throws core::FileIOException when @p index is out of range or the seek/write/flush fails.
     *
     * /// @oop-concept Random Access File IO :: seekp to an arbitrary slot, then write over it
     */
    void overwriteAt(std::size_t index, const AuditRecord& rec);

    /// @brief Additive checksum over bytes `[0,124)` of @p rec — everything except the field itself.
    /// @oop-concept Static Members :: a pure function of its argument, so it needs no object
    static std::uint32_t checksumOf(const AuditRecord& rec);

    /// @brief Copies @p src into a fixed char buffer as NUL-padded ASCII, truncating if needed.
    /// @param cap capacity of @p dest in bytes, including the padding.
    static void fillString(char* dest, std::size_t cap, const QString& src);

protected:
    /// @throws core::FileIOException("audit file not open") when the stream is closed.
    void ensureOpen();

    std::fstream m_stream;   ///< opened in | out | binary — read and write on one handle
    QString m_path;          ///< file this object owns
};

} // namespace aluchop::persistence

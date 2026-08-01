#pragma once

/**
 * @file AuditTrail.hpp
 * @brief Sequenced, checksummed audit log built on top of the raw record file.
 *
 * `AuditTrail` adds the two invariants a record file knows nothing about: sequence numbers are
 * 1-based and strictly increasing, and every record carries a checksum computed at write time.
 * Those invariants only hold if nobody can write a record behind the trail's back — which is
 * exactly why the base class is inherited privately.
 */

#include <cstddef>
#include <cstdint>
#include <vector>

#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/persistence/BinaryRecordFile.hpp"

namespace aluchop::persistence {

/**
 * @brief The application's tamper-evident audit log.
 *
 * /// @oop-concept Private Inheritance :: an AuditTrail is IMPLEMENTED-IN-TERMS-OF a record file,
 * /// it is not one — private inheritance stops callers reaching append()/overwriteAt() and
 * /// bypassing the sequencing and checksum invariants this class exists to guarantee.
 */
class AuditTrail : private BinaryRecordFile {
public:
    /**
     * @brief Opens (creating if needed) the trail and resumes numbering after the last record.
     * @param path absolute path of `audit.bin`.
     * @throws core::FileIOException when the file cannot be opened or its size cannot be read.
     */
    explicit AuditTrail(const QString& path);

    /**
     * @brief Writes one audit record and returns the sequence number it was given.
     * @param userId acting user id, 0 for system actions.
     * @param action short verb, e.g. `LOGIN`, `ORDER_PAID`, `RESTOCK` (truncated to 15 chars).
     * @param entity what was acted on, e.g. `order:42` (truncated to 15 chars).
     * @param amount money involved; `core::Money()` when the action is not monetary.
     * @param details free text, truncated to 63 characters.
     * @throws core::FileIOException when the underlying write fails — auditing never fails silently.
     */
    std::uint32_t record(std::uint32_t userId, const QString& action, const QString& entity,
                         core::Money amount, const QString& details);

    /**
     * @brief Reads an arbitrary record by index — the visible proof of random access.
     * @throws core::FileIOException on a bad index or a failed integrity check.
     */
    AuditRecord at(std::size_t index);

    /// @brief The last @p n records, oldest first (fewer when the trail is shorter).
    std::vector<AuditRecord> tail(std::size_t n);

    /// @return how many records the trail holds.
    std::size_t size();

    /**
     * @brief Walks every record and verifies magic + checksum.
     * @param[out] firstBadIndex index of the first damaged record; untouched when the trail is clean.
     * @return true when every record verifies.
     * /// @oop-concept Pass by Reference :: the caller learns *where* corruption starts, not just that it does
     */
    bool verifyIntegrity(std::size_t& firstBadIndex);

    /// Selective re-export: closing is safe for callers, writing raw records is not.
    using BinaryRecordFile::close;

private:
    std::uint32_t m_nextSeq = 1;   ///< resumed from recordCount() + 1 at construction
};

} // namespace aluchop::persistence

/**
 * @file AuditTrail.cpp
 * @brief The tamper-evident audit log layered over the raw record file.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * `AuditTrail` is **implemented in terms of** `BinaryRecordFile` — hence private inheritance. The
 * base class can write any 128 bytes anywhere; the trail may not, because it guarantees two things
 * the base knows nothing about:
 *
 *  1. `seq` is 1-based and increases by exactly one per record;
 *  2. every stored record carries a checksum computed at write time.
 *
 * Private inheritance is what enforces that: `append()` and `overwriteAt()` are unreachable from
 * outside, so the only way a record reaches the file is through record(), which maintains both
 * invariants. Only `close()` is re-exported, because closing cannot break either of them.
 */

#include "aluchop/persistence/AuditTrail.hpp"

#include <ios>
#include <string>

#include <QDateTime>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::persistence {

AuditTrail::AuditTrail(const QString& path) : BinaryRecordFile(path)
{
    openOrCreate();

    // Resume numbering after whatever the previous run left behind. The last record's own sequence
    // number is authoritative; the record count is only the fallback for a damaged tail.
    const std::size_t existing = recordCount();
    m_nextSeq = static_cast<std::uint32_t>(existing) + 1u;
    if (existing > 0) {
        try {
            const AuditRecord last = readAt(existing - 1);
            m_nextSeq = last.seq + 1u;
        } catch (const core::FileIOException&) {
            // A corrupt final record must not stop the application from auditing anything further;
            // verifyIntegrity() is what reports the damage, and the count-based fallback still
            // yields a sequence number no earlier record can collide with.
            m_nextSeq = static_cast<std::uint32_t>(existing) + 1u;
        }
    }
}

std::uint32_t AuditTrail::record(std::uint32_t userId, const QString& action, const QString& entity,
                                 core::Money amount, const QString& details)
{
    AuditRecord rec{}; // value-initialised: every byte written to disk is one we chose

    rec.timestampUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    rec.amountPaisa    = amount.paisa();  // integer paisa — currency never touches a double
    rec.magic          = kAuditRecordMagic;
    rec.seq            = m_nextSeq;
    rec.userId         = userId;
    fillString(rec.action, sizeof(rec.action), action);
    fillString(rec.entity, sizeof(rec.entity), entity);
    fillString(rec.details, sizeof(rec.details), details);
    rec.checksum = checksumOf(rec); // computed last: it covers bytes [0,124) only

    append(rec); // throws FileIOException on any stream failure — auditing never fails silently

    ++m_nextSeq;
    return rec.seq;
}

AuditRecord AuditTrail::at(std::size_t index)
{
    return readAt(index);
}

std::vector<AuditRecord> AuditTrail::tail(std::size_t n)
{
    std::vector<AuditRecord> out;
    if (n == 0) {
        return out;
    }

    const std::size_t total = recordCount();
    if (total == 0) {
        return out;
    }
    const std::size_t first = (total > n) ? (total - n) : 0;
    out.reserve(total - first);

    for (std::size_t i = first; i < total; ++i) {
        out.push_back(readAt(i)); // oldest first
    }
    return out;
}

std::size_t AuditTrail::size()
{
    return recordCount();
}

bool AuditTrail::verifyIntegrity(std::size_t& firstBadIndex)
{
    ensureOpen();

    const std::size_t total = recordCount();
    std::uint32_t previousSeq = 0;

    for (std::size_t i = 0; i < total; ++i) {
        // Deliberately NOT readAt(): that helper throws on the first damaged record, whereas an
        // integrity report has to survive the damage long enough to name where it starts.
        m_stream.clear();
        m_stream.seekg(static_cast<std::streamoff>(i * sizeof(AuditRecord)), std::ios::beg);
        if (m_stream.fail()) {
            m_stream.clear();
            firstBadIndex = i;
            return false;
        }

        AuditRecord rec{};
        m_stream.read(reinterpret_cast<char*>(&rec), static_cast<std::streamsize>(sizeof(AuditRecord)));
        const bool fullRecordRead =
            m_stream.good() && m_stream.gcount() == static_cast<std::streamsize>(sizeof(AuditRecord));
        m_stream.clear();

        if (!fullRecordRead || rec.magic != kAuditRecordMagic || rec.checksum != checksumOf(rec) ||
            rec.seq <= previousSeq) {
            /// @oop-concept Pass by Reference :: the out-parameter tells the caller exactly which
            /// record is the first damaged one, which a bare bool never could.
            firstBadIndex = i;
            return false;
        }
        previousSeq = rec.seq;
    }
    return true; // firstBadIndex is left untouched when the trail is clean
}

} // namespace aluchop::persistence

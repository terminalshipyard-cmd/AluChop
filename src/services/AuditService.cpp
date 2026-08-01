/**
 * @file AuditService.cpp
 * @brief Dual-write audit logging and the GUI-lawful window onto the binary trail.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Write order is deliberate and binding: the fixed-record binary trail first, the SQLite mirror
 * second. The binary file is the *evidence* — 128-byte records, sequence-numbered and checksummed,
 * on a medium the application's own SQL layer cannot rewrite. The `audit_log` table is only a
 * convenience index that makes the same events queryable by the Reports screen.
 *
 * Failure policy, also binding:
 *  - a failed **trail** write is fatal to the operation: it is written to `core::Logger`, mirrored
 *    with sequence 0 so the attempt itself is not lost, and then re-thrown with a bare `throw;`;
 *  - a failed **mirror** write is not: the evidence is already durable, so the SQL failure is
 *    logged and swallowed rather than aborting a business operation that genuinely succeeded.
 */

#include "aluchop/services/AuditService.hpp"

#include <QDateTime>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Logger.hpp"

namespace aluchop::services {

namespace {

/**
 * @brief Writes to `core::Logger` without ever letting the logger's own failure escape.
 *
 * Every call site below is either inside a `catch` block or on a path that must not acquire a
 * second failure mode. `Logger::log` throws `core::FileIOException` when `logs/aluchop.log`
 * cannot be appended to, and a throw from inside a handler would replace the real diagnosis with
 * a much less interesting one.
 */
void quietLog(core::Logger::Level level, const QString& message) {
    try {
        core::Logger::instance().log(level, message);
    } catch (...) {
        // Deliberately empty: losing a log line is acceptable, losing the original exception is not.
    }
}

} // namespace

AuditService::AuditService(persistence::AuditTrail& trail, persistence::AuditRepository& mirror)
    : m_trail(trail), m_mirror(mirror) {
    // Both sinks are owned by AppContext and merely borrowed; this service opens nothing and
    // closes nothing, which is what lets it be constructed after the trail and destroyed before it.
}

void AuditService::setActiveUser(int userId) {
    m_activeUserId = userId > 0 ? userId : 0;   // negative ids are meaningless; 0 means "system"
}

/// @oop-concept Function Overloading :: the short form is the full form with the parts a caller
/// without money or detail simply does not have
void AuditService::log(const QString& action, const QString& entity) {
    log(action, entity, core::Money(), QString());
}

void AuditService::log(const QString& action, const QString& entity,
                       core::Money amount, const QString& details) {
    const qint64 tsUtcMs = QDateTime::currentMSecsSinceEpoch();
    const auto userId = static_cast<std::uint32_t>(m_activeUserId);

    std::uint32_t seq = 0;
    try {
        // 1. The authoritative medium. AuditTrail assigns and returns the sequence number.
        seq = m_trail.record(userId, action, entity, amount, details);
    } catch (const core::FileIOException& e) {
        quietLog(core::Logger::Level::Error,
                 QStringLiteral("audit trail write FAILED for %1/%2: %3")
                     .arg(action, entity, QString::fromUtf8(e.what())));

        // Mirror the attempt anyway, with sequence 0 — a row whose seq is 0 is precisely the
        // signature of "the binary trail refused this record", and a reviewer comparing the two
        // media can see the gap instead of finding nothing at all.
        try {
            m_mirror.insert(0, tsUtcMs, m_activeUserId, action, entity, amount,
                            details.isEmpty() ? QStringLiteral("[trail write failed]")
                                              : details + QStringLiteral(" [trail write failed]"));
        } catch (const core::AluChopException& mirrorFailure) {
            quietLog(core::Logger::Level::Error,
                     QStringLiteral("audit mirror also FAILED: %1")
                         .arg(QString::fromUtf8(mirrorFailure.what())));
        }

        /// @oop-concept Rethrow :: the failure is recorded and reported, then continues to
        /// propagate completely untouched — an audit trail that fails silently is worse than none
        throw;
    }

    // 2. The queryable mirror. Its sequence column carries the number the trail assigned, which is
    //    what lets the two media be cross-checked record for record.
    try {
        m_mirror.insert(seq, tsUtcMs, m_activeUserId, action, entity, amount, details);
    } catch (const core::AluChopException& e) {
        // The evidence is already on disk; the index is not worth aborting a real operation over.
        quietLog(core::Logger::Level::Warn,
                 QStringLiteral("audit mirror write failed for seq %1: %2")
                     .arg(seq).arg(QString::fromUtf8(e.what())));
    }
}

// ---------------------------------------------------------------------------------------------
// GUI-lawful surface over the binary trail (docs/ARCHITECTURE.md §12, reconciliation R1).
//
// The GUI layer may not name persistence types, so ReportsPage cannot touch AuditTrail directly.
// These four methods are the whole of its access, and they honour the service-layer contract:
// expected outcomes come back as values, never as exceptions — with the single documented
// exception of trailRecordAt(), whose header explicitly promises a throw on a bad index.
// ---------------------------------------------------------------------------------------------

bool AuditService::verifyTrailIntegrity(std::size_t& firstBadIndex) {
    try {
        return m_trail.verifyIntegrity(firstBadIndex);
    } catch (const core::FileIOException& e) {
        // A trail that cannot even be read has certainly not been verified.
        quietLog(core::Logger::Level::Error,
                 QStringLiteral("audit verification could not read the trail: %1")
                     .arg(QString::fromUtf8(e.what())));
        return false;
    }
}

std::size_t AuditService::trailRecordCount() {
    try {
        return m_trail.size();
    } catch (const core::FileIOException& e) {
        quietLog(core::Logger::Level::Error,
                 QStringLiteral("audit trail size unavailable: %1").arg(QString::fromUtf8(e.what())));
        return 0;
    }
}

/// @oop-concept Random Access File IO :: one arbitrary 128-byte record fetched by index alone,
/// surfaced all the way to the Reports screen as a record browser
persistence::AuditRecord AuditService::trailRecordAt(std::size_t index) {
    // Deliberately *not* caught: the header documents this as throwing, because asking for a
    // record that does not exist is a caller bug, and silently returning a zeroed record would
    // put fabricated audit data on screen.
    return m_trail.at(index);
}

std::vector<persistence::AuditRecord> AuditService::recentTrailRecords(std::size_t n) {
    try {
        return m_trail.tail(n);
    } catch (const core::FileIOException& e) {
        quietLog(core::Logger::Level::Error,
                 QStringLiteral("audit trail tail unavailable: %1").arg(QString::fromUtf8(e.what())));
        return {};
    }
}

} // namespace aluchop::services

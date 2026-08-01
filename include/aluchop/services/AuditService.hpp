#pragma once

/**
 * @file AuditService.hpp
 * @brief The single entry point through which anything is written to the audit trail.
 *
 * Every mutating action in AluChop (login, order created, order paid, restock, backup, settings
 * change, user created) funnels through here. The service writes twice: first the authoritative
 * fixed-record binary trail, then the queryable SQLite mirror. Writing in that order matters — the
 * binary file is the evidence, the table is only a convenience index into it.
 */

#include <cstddef>
#include <vector>

#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/persistence/AuditRepository.hpp"
#include "aluchop/persistence/AuditTrail.hpp"
#include "aluchop/persistence/BinaryRecordFile.hpp"   // persistence::AuditRecord, returned by value

namespace aluchop::services {

/// @brief Records who did what, when, to which entity, for how much money.
class AuditService {
public:
    /// @oop-concept Pass by Reference :: both sinks are owned by AppContext and merely borrowed here
    AuditService(persistence::AuditTrail& trail, persistence::AuditRepository& mirror);

    /// @brief Sets the user stamped on subsequent records; `AuthService` calls this on login and
    ///        passes 0 on logout.
    void setActiveUser(int userId);

    /**
     * @brief Records a non-monetary action.
     * /// @oop-concept Function Overloading :: the same verb with the detail the caller actually has
     */
    void log(const QString& action, const QString& entity);

    /**
     * @brief Records an action that involved money and/or extra detail.
     * @throws core::FileIOException when the binary trail cannot be written. The failure is first
     *         written to `core::Logger` and then re-thrown with a bare `throw;` — an audit trail
     *         that fails silently is worse than no audit trail at all.
     *
     * /// @oop-concept Default Arguments :: details are optional; the amount usually is not
     * /// @oop-concept Rethrow :: log the IO failure, then let it keep propagating untouched
     */
    void log(const QString& action, const QString& entity,
             core::Money amount, const QString& details = QString());

    /**
     * @brief Walks the whole binary trail and verifies every record's magic and checksum.
     * @param[out] firstBadIndex index of the first damaged record; untouched when the trail is clean.
     * @return true when every record verifies.
     *
     * This is the lawful path for the Reports screen's "Verify audit" action. The GUI layer may
     * only depend on services / models / core, so it must never name persistence::AuditTrail nor
     * include its header; this wrapper is what keeps that rule true while still surfacing the
     * random-access file layer as a user-visible feature.
     *
     * /// @oop-concept Pass by Reference :: the caller learns *where* corruption starts, not just that it does
     */
    bool verifyTrailIntegrity(std::size_t& firstBadIndex);

    /// @brief Number of records currently in the binary trail (shown beside the verify result).
    std::size_t trailRecordCount();

    /**
     * @brief Reads one arbitrary record by index — the random-access proof, surfaced as a browser.
     * @throws core::FileIOException on a bad index or a failed integrity check.
     */
    persistence::AuditRecord trailRecordAt(std::size_t index);

    /// @brief The most recent @p n records, oldest first.
    std::vector<persistence::AuditRecord> recentTrailRecords(std::size_t n);

    /**
     * @brief Direct access to the trail, for **services-layer** wiring and tests only.
     *
     * @warning Not a GUI entry point. Reaching through this from `aluchop::gui` would force a GUI
     *          translation unit to include a persistence header and break the layer rule — use
     *          verifyTrailIntegrity() / trailRecordAt() / recentTrailRecords() instead.
     *
     * /// @oop-concept Return by Reference :: the caller inspects the live trail, it never gets copied
     */
    persistence::AuditTrail& trail() { return m_trail; }

private:
    persistence::AuditTrail& m_trail;         ///< authoritative binary log
    persistence::AuditRepository& m_mirror;   ///< queryable SQL mirror
    int m_activeUserId = 0;                   ///< 0 = system / nobody logged in
};

} // namespace aluchop::services

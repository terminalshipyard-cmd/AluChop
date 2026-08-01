#pragma once

/**
 * @file AuditRepository.hpp
 * @brief SQLite mirror of the binary audit trail, so the trail is queryable.
 *
 * The authoritative audit record is the fixed-record binary file written by @ref AuditTrail.
 * This table is a convenience mirror: it carries the same `seq` numbers, which is what lets a
 * reviewer cross-check the two and spot tampering.
 */

#include <tuple>
#include <vector>

#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include "aluchop/core/Money.hpp"

namespace aluchop::persistence {

/// @brief Append-and-read access to the `audit_log` table (never updated, never deleted).
class AuditRepository {
public:
    AuditRepository() = default;

    /**
     * @brief Mirrors one audit record into SQL.
     * @param seq the sequence number the binary trail assigned to this very record.
     * @param tsUtcMs milliseconds since the Unix epoch, UTC.
     * @param userId acting user, 0 for system actions.
     */
    void insert(quint32 seq, qint64 tsUtcMs, int userId, const QString& action,
                const QString& entity, core::Money amount, const QString& details);

    /**
     * @brief Most recent audit rows, newest first.
     * @return tuples of (seq, timestamp, user id, action, entity, amount, details).
     * /// @oop-concept STL (vector/tuple) :: a read-only projection needs no dedicated model class
     */
    std::vector<std::tuple<quint32, QDateTime, int, QString, QString, core::Money, QString>>
        recent(int limit = 100) const;
};

} // namespace aluchop::persistence

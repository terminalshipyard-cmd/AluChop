/**
 * @file NotificationService.cpp
 * @brief Implementation of the application-wide event bus.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * This is the one and only translation unit in `aluchop::services` that participates in Qt's
 * meta-object system. Every other service is a plain C++ class holding references, which is what
 * keeps the business layer free of moc and testable without an event loop.
 */

#include "aluchop/services/NotificationService.hpp"

#include "aluchop/core/Algorithms.hpp"
#include "aluchop/models/Enums.hpp"

namespace aluchop::services {

namespace {

/// @brief Lowest / highest legal value of models::NoticeLevel widened to int.
constexpr int kMinLevel = 0; ///< models::NoticeLevel::Info
constexpr int kMaxLevel = 3; ///< models::NoticeLevel::Danger

/**
 * @brief The eight domain names the GUI pages listen for.
 *
 * Announcing an unknown domain is a programming slip that would silently stop a page refreshing,
 * so it is corrected to the catch-all `"all"` (which every page treats as "reload me") rather than
 * being forwarded verbatim.
 */
/// @oop-concept Object Arrays :: the legal domain vocabulary as one const array
const QString kDomains[] = {
    QStringLiteral("menu"),         QStringLiteral("orders"),
    QStringLiteral("customers"),    QStringLiteral("employees"),
    QStringLiteral("inventory"),    QStringLiteral("reservations"),
    QStringLiteral("payments"),     QStringLiteral("settings")
};

/// @brief Whether @p domain is one of the eight documented domain names.
bool isKnownDomain(const QString& domain) {
    for (const QString& d : kDomains)
        if (d == domain) return true;
    return false;
}

/**
 * @brief Human title used when a caller supplies none.
 * @param level already-clamped severity.
 *
 * `models::toString(NoticeLevel)` deliberately yields the database token ("INFO", "DANGER"),
 * which is the wrong register for a toast header — hence this small presentation-side mapping.
 */
QString levelTitle(int level) {
    switch (static_cast<models::NoticeLevel>(level)) {
    case models::NoticeLevel::Success: return QStringLiteral("Success");
    case models::NoticeLevel::Warning: return QStringLiteral("Warning");
    case models::NoticeLevel::Danger:  return QStringLiteral("Error");
    case models::NoticeLevel::Info:    break;
    }
    return QStringLiteral("Notice");
}

} // namespace

NotificationService::NotificationService(QObject* parent)
    : QObject(parent) {
    // Nothing else to do: the bus owns no state at all. Its entire job is to decouple the
    // services that *cause* events from the widgets that *render* them, so that neither side
    // needs a pointer to the other.
}

void NotificationService::notify(const QString& title, const QString& message, int level) {
    // A malformed level would index straight off the end of the toast palette in the GUI, so it is
    // clamped here — at the single place every notice passes through — rather than in each widget.
    /// @oop-concept Function Template :: core::clampValue constrains the severity without a copy
    const int safeLevel = core::clampValue(level, kMinLevel, kMaxLevel);

    // Titles are always shown; an empty title would render as a blank toast header, so it falls
    // back to the severity's own name ("Info", "Success", "Warning", "Danger").
    QString safeTitle = title.trimmed();
    if (safeTitle.isEmpty())
        safeTitle = levelTitle(safeLevel);

    emit notification(safeTitle, message, safeLevel);
}

void NotificationService::announceDataChanged(const QString& domain) {
    const QString d = domain.trimmed().toLower();
    emit dataChanged(isKnownDomain(d) ? d : QStringLiteral("all"));
}

} // namespace aluchop::services

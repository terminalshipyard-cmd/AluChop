#pragma once

/// \file
/// \brief Closed domain vocabularies of the AluChop domain model, plus their
///        exact SQLite token mappings.
///
/// Every enumerator maps to one — and only one — TEXT token accepted by the
/// `CHECK` constraints declared in the SQLite schema. Keeping the mapping in the
/// model layer (rather than in each repository) guarantees that persistence,
/// services and the GUI can never disagree about what `'DINE_IN'` means.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QString>

namespace aluchop::models {

/// @oop-concept Enumerations :: scoped enums for every closed domain vocabulary
/// \brief How an order is fulfilled.
enum class OrderType {
    DineIn,   ///< Guest eats at a table in the restaurant. Requires an active table.
    Takeaway, ///< Guest collects the food at the counter.
    Delivery  ///< Food is dispatched to the guest's address.
};

/// \brief Lifecycle of an order, from creation to settlement.
///
/// The legal transitions are enforced by Order::setStatus():
/// `Open → {Pending, Cancelled}`, `Pending → {Preparing, Cancelled}`,
/// `Preparing → Ready`, `Ready → Served`, `Served → Paid`.
enum class OrderStatus {
    Open,      ///< Being edited by a waiter; not yet visible to the kitchen.
    Pending,   ///< Submitted to the kitchen queue, not started.
    Preparing, ///< Chef is cooking it.
    Ready,     ///< Ready on the pass, waiting to be carried out.
    Served,    ///< Delivered to the guest; inventory has been deducted.
    Paid,      ///< Settled by BillingService; a Payment row exists.
    Cancelled  ///< Voided, or absorbed into another order by a merge.
};

/// \brief Tender types accepted at the till.
enum class PaymentMethod {
    Cash,   ///< The only method that can produce change.
    Card,   ///< Debit/credit; tendered is forced to the exact total.
    Wallet  ///< Digital wallet (eSewa/Khalti style); exact total.
};

/// \brief Whether a promo code takes a percentage off or a flat amount off.
enum class PromoKind {
    Percent, ///< Discount = subtotal.percent(Promo::percent()).
    Flat     ///< Discount = min(Promo::flatAmount(), subtotal).
};

/// \brief Lifecycle of a table booking.
enum class ReservationStatus {
    Booked,    ///< Confirmed, guest not yet arrived. Blocks the table.
    Seated,    ///< Guest has arrived and is at the table. Blocks the table.
    Completed, ///< Guest has left; the slot is free again.
    Cancelled, ///< Called off in advance.
    NoShow     ///< Guest never arrived.
};

/// \brief Authorisation role attached to a login account.
enum class UserRole {
    Admin,   ///< Full access, including user management.
    Manager, ///< Everything except user management.
    Waiter,  ///< Orders, customers, reservations.
    Chef     ///< Kitchen board only.
};

/// \brief Severity of a user-facing notification (toast colour + icon).
enum class NoticeLevel {
    Info,    ///< Neutral information.
    Success, ///< An operation completed.
    Warning, ///< Attention needed (low stock, expiring goods).
    Danger   ///< An operation failed.
};

// ---------------------------------------------------------------------------
// Enum <-> database-token mapping.
//
// @oop-concept Function Overloading :: one verb `toString` serves seven unrelated
// enum types — the compiler selects the right conversion, so no call site ever has
// to invent a name like `orderTypeToString`.
//
// Every *FromString() throws core::ValidationException when handed a token that is
// not in the corresponding SQLite CHECK list, so a corrupted row fails loudly at
// hydration time instead of silently becoming enumerator 0.
// ---------------------------------------------------------------------------

/// \brief Convert an OrderType to its database token ("DINE_IN" / "TAKEAWAY" / "DELIVERY").
QString toString(OrderType v);
/// \brief Parse an OrderType database token.
/// \throws core::ValidationException when \p s is not a recognised token.
OrderType orderTypeFromString(const QString& s);

/// \brief Convert an OrderStatus to its database token ("OPEN" … "CANCELLED").
QString toString(OrderStatus v);
/// \brief Parse an OrderStatus database token.
/// \throws core::ValidationException when \p s is not a recognised token.
OrderStatus orderStatusFromString(const QString& s);

/// \brief Convert a PaymentMethod to its database token ("CASH" / "CARD" / "WALLET").
QString toString(PaymentMethod v);
/// \brief Parse a PaymentMethod database token.
/// \throws core::ValidationException when \p s is not a recognised token.
PaymentMethod paymentMethodFromString(const QString& s);

/// \brief Convert a PromoKind to its database token ("PERCENT" / "FLAT").
QString toString(PromoKind v);
/// \brief Parse a PromoKind database token.
/// \throws core::ValidationException when \p s is not a recognised token.
PromoKind promoKindFromString(const QString& s);

/// \brief Convert a ReservationStatus to its database token ("BOOKED" … "NO_SHOW").
QString toString(ReservationStatus v);
/// \brief Parse a ReservationStatus database token.
/// \throws core::ValidationException when \p s is not a recognised token.
ReservationStatus reservationStatusFromString(const QString& s);

/// \brief Convert a UserRole to its database token ("ADMIN" / "MANAGER" / "WAITER" / "CHEF").
QString toString(UserRole v);
/// \brief Parse a UserRole database token.
/// \throws core::ValidationException when \p s is not a recognised token.
UserRole userRoleFromString(const QString& s);

/// \brief Convert a NoticeLevel to its token ("INFO" / "SUCCESS" / "WARNING" / "DANGER").
QString toString(NoticeLevel v);
/// \brief Parse a NoticeLevel token.
/// \throws core::ValidationException when \p s is not a recognised token.
NoticeLevel noticeLevelFromString(const QString& s);

} // namespace aluchop::models

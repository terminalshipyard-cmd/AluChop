/// \file
/// \brief Enum ↔ SQLite-token conversion tables for every closed vocabulary of
///        the domain model.
///
/// Each enum owns exactly one table of `{enumerator, token}` pairs, and both
/// directions of the conversion read that same table. There is therefore no way
/// for `toString()` and `…FromString()` to drift apart, and adding an
/// enumerator without giving it a token is caught the first time it is
/// converted rather than by a `CHECK` constraint failing inside the SQLite
/// driver.
///
/// The tokens here are literally the strings named in the schema's `CHECK`
/// constraints (ARCHITECTURE §6) — `'DINE_IN'`, `'NO_SHOW'`, `'CANCELLED'` and
/// the rest.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Enums.hpp"

#include <array>
#include <string>
#include <utility>

#include <QLatin1String>
#include <QString>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::models {
namespace {

/// \brief One row of a conversion table.
/// @oop-concept Structures :: a two-field aggregate with no invariants to protect
template <typename E>
struct TokenRow {
    E value;            ///< The enumerator.
    const char* token;  ///< Its exact database token.
};

/// @oop-concept Function Template :: one lookup serves all seven unrelated enums
/// \brief Finds the database token of an enumerator.
/// \param table Conversion table for this enum type.
/// \param v     Enumerator to convert.
/// \param what  Enum name, used in the error message.
/// \return The token as a QString.
/// \throws core::ValidationException when the enumerator has no token (i.e. a
///         new enumerator was added without extending the table).
template <typename E, std::size_t N>
QString tokenOf(const std::array<TokenRow<E>, N>& table, E v, const char* what)
{
    for (const TokenRow<E>& row : table) {
        if (row.value == v)
            return QString::fromLatin1(row.token);
    }
    throw core::ValidationException(std::string("unmapped ") + what + " enumerator");
}

/// @oop-concept Function Template :: the inverse lookup, equally generic
/// \brief Parses a database token back into its enumerator.
/// \param table Conversion table for this enum type.
/// \param s     Token read from SQLite (trimmed and upper-cased before matching).
/// \param what  Enum name, used in the error message.
/// \return The matching enumerator.
/// \throws core::ValidationException when the token is not in the table, so a
///         corrupted row fails loudly instead of silently becoming enumerator 0.
template <typename E, std::size_t N>
E valueOf(const std::array<TokenRow<E>, N>& table, const QString& s, const char* what)
{
    const QString needle = s.trimmed().toUpper();
    for (const TokenRow<E>& row : table) {
        if (needle == QLatin1String(row.token))
            return row.value;
    }
    throw core::ValidationException(std::string("unknown ") + what + " token '"
                                    + s.toStdString() + "'");
}

// --- The seven tables. These strings ARE the schema CHECK lists. ------------

/// @oop-concept Object Arrays :: each vocabulary is one constant array of rows
const std::array<TokenRow<OrderType>, 3> kOrderTypes{{
    {OrderType::DineIn, "DINE_IN"},
    {OrderType::Takeaway, "TAKEAWAY"},
    {OrderType::Delivery, "DELIVERY"},
}};

const std::array<TokenRow<OrderStatus>, 7> kOrderStatuses{{
    {OrderStatus::Open, "OPEN"},
    {OrderStatus::Pending, "PENDING"},
    {OrderStatus::Preparing, "PREPARING"},
    {OrderStatus::Ready, "READY"},
    {OrderStatus::Served, "SERVED"},
    {OrderStatus::Paid, "PAID"},
    {OrderStatus::Cancelled, "CANCELLED"},
}};

const std::array<TokenRow<PaymentMethod>, 3> kPaymentMethods{{
    {PaymentMethod::Cash, "CASH"},
    {PaymentMethod::Card, "CARD"},
    {PaymentMethod::Wallet, "WALLET"},
}};

const std::array<TokenRow<PromoKind>, 2> kPromoKinds{{
    {PromoKind::Percent, "PERCENT"},
    {PromoKind::Flat, "FLAT"},
}};

const std::array<TokenRow<ReservationStatus>, 5> kReservationStatuses{{
    {ReservationStatus::Booked, "BOOKED"},
    {ReservationStatus::Seated, "SEATED"},
    {ReservationStatus::Completed, "COMPLETED"},
    {ReservationStatus::Cancelled, "CANCELLED"},
    {ReservationStatus::NoShow, "NO_SHOW"},
}};

const std::array<TokenRow<UserRole>, 4> kUserRoles{{
    {UserRole::Admin, "ADMIN"},
    {UserRole::Manager, "MANAGER"},
    {UserRole::Waiter, "WAITER"},
    {UserRole::Chef, "CHEF"},
}};

const std::array<TokenRow<NoticeLevel>, 4> kNoticeLevels{{
    {NoticeLevel::Info, "INFO"},
    {NoticeLevel::Success, "SUCCESS"},
    {NoticeLevel::Warning, "WARNING"},
    {NoticeLevel::Danger, "DANGER"},
}};

} // namespace

// ---------------------------------------------------------------------------
// @oop-concept Function Overloading :: one verb `toString` serves seven unrelated
// types — the compiler picks the right table, so no call site invents a name.
// ---------------------------------------------------------------------------

QString toString(OrderType v)
{
    return tokenOf(kOrderTypes, v, "OrderType");
}

OrderType orderTypeFromString(const QString& s)
{
    return valueOf(kOrderTypes, s, "OrderType");
}

QString toString(OrderStatus v)
{
    return tokenOf(kOrderStatuses, v, "OrderStatus");
}

OrderStatus orderStatusFromString(const QString& s)
{
    return valueOf(kOrderStatuses, s, "OrderStatus");
}

QString toString(PaymentMethod v)
{
    return tokenOf(kPaymentMethods, v, "PaymentMethod");
}

PaymentMethod paymentMethodFromString(const QString& s)
{
    return valueOf(kPaymentMethods, s, "PaymentMethod");
}

QString toString(PromoKind v)
{
    return tokenOf(kPromoKinds, v, "PromoKind");
}

PromoKind promoKindFromString(const QString& s)
{
    return valueOf(kPromoKinds, s, "PromoKind");
}

QString toString(ReservationStatus v)
{
    return tokenOf(kReservationStatuses, v, "ReservationStatus");
}

ReservationStatus reservationStatusFromString(const QString& s)
{
    return valueOf(kReservationStatuses, s, "ReservationStatus");
}

QString toString(UserRole v)
{
    return tokenOf(kUserRoles, v, "UserRole");
}

UserRole userRoleFromString(const QString& s)
{
    return valueOf(kUserRoles, s, "UserRole");
}

QString toString(NoticeLevel v)
{
    return tokenOf(kNoticeLevels, v, "NoticeLevel");
}

NoticeLevel noticeLevelFromString(const QString& s)
{
    return valueOf(kNoticeLevels, s, "NoticeLevel");
}

} // namespace aluchop::models

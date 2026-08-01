/// \file
/// \brief Translation unit for models::Payment.
///
/// Payment is a persisted *ledger row*: every one of its members is a validated
/// field with no derived arithmetic, so the whole class is expressible as inline
/// accessors in the header and there is deliberately no out-of-line definition to
/// write here. All of the money arithmetic happened in services::BillingService
/// before a Payment was ever constructed, which is exactly the property that lets
/// a revenue report read back what the guest actually paid instead of re-deriving
/// it from a menu that may have been re-priced since.
///
/// What this file does carry is the mechanical enforcement of the two rules that
/// the header can only state in prose: every monetary field really is
/// core::Money (never a floating-point type), and Payment really is the plain
/// copyable value the repository layer assumes when it hydrates rows into a
/// std::vector<Payment>. These are compile-time checks — if a later edit turned
/// `total()` into a `double`, this translation unit would stop compiling.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Payment.hpp"

#include <type_traits>

#include "aluchop/core/Money.hpp"

namespace aluchop::models {
namespace {

/// \brief The exact type every money accessor on Payment must return.
using MoneyReturn = core::Money;

/// @oop-concept Constant Member Functions :: every money observer is const and
/// returns the value type, so no caller can reach a raw number to do float maths on
static_assert(std::is_same<decltype(std::declval<const Payment&>().subtotal()), MoneyReturn>::value,
              "Payment::subtotal() must return core::Money — currency is never a double (SPEC section 0)");
static_assert(std::is_same<decltype(std::declval<const Payment&>().discount()), MoneyReturn>::value,
              "Payment::discount() must return core::Money — currency is never a double (SPEC section 0)");
static_assert(std::is_same<decltype(std::declval<const Payment&>().serviceCharge()), MoneyReturn>::value,
              "Payment::serviceCharge() must return core::Money — currency is never a double (SPEC section 0)");
static_assert(std::is_same<decltype(std::declval<const Payment&>().total()), MoneyReturn>::value,
              "Payment::total() must return core::Money — currency is never a double (SPEC section 0)");
static_assert(std::is_same<decltype(std::declval<const Payment&>().tendered()), MoneyReturn>::value,
              "Payment::tendered() must return core::Money — currency is never a double (SPEC section 0)");
static_assert(std::is_same<decltype(std::declval<const Payment&>().change()), MoneyReturn>::value,
              "Payment::change() must return core::Money — currency is never a double (SPEC section 0)");

/// \brief Value semantics relied on by PaymentRepository and every report query.
static_assert(std::is_default_constructible<Payment>::value,
              "PaymentRepository default-constructs a Payment before hydrating it");
static_assert(std::is_copy_constructible<Payment>::value && std::is_copy_assignable<Payment>::value,
              "Payments are carried around by value in std::vector<Payment>");
static_assert(std::is_move_constructible<Payment>::value && std::is_move_assignable<Payment>::value,
              "Payments must relocate cheaply when a report vector grows");
static_assert(!std::is_polymorphic<Payment>::value,
              "Payment is a ledger row, not a polymorphic entity — it carries no vtable");

} // namespace
} // namespace aluchop::models

#pragma once
/**
 * @file Money.hpp
 * @brief NPR currency value type stored as integer paisa — never floating point.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Every amount in AluChop (menu prices, bills, salaries, revenue) is an
 * aluchop::core::Money. Currency is held as a 64-bit signed count of *paisa*
 * (1 rupee = 100 paisa), so no rounding drift can ever accumulate the way it
 * does with `double`. Formatting to human text happens only at the
 * presentation edge, via toString() / formatNpr().
 *
 * Menu prices in this system are already **tax inclusive**; no operation in
 * this class or anywhere downstream adds tax on top of a price.
 */

#include <cstdint>
#include <iosfwd>

#include <QString>

namespace aluchop::core {

/**
 * @brief An amount of Nepalese Rupees held as integer paisa.
 *
 * Money behaves like a built-in arithmetic type: it can be added, subtracted,
 * negated, scaled by an integer quantity and fully ordered. It deliberately
 * exposes no floating-point interface at all.
 */
/// @oop-concept Operator Overloading :: money is a true value type — arithmetic/relational operators replace error-prone raw integers
class Money {
public:
    /**
     * @brief Constructs a zero amount (Rs 0.00).
     */
    constexpr Money() noexcept = default;

    /**
     * @brief Constructs an amount directly from a paisa count.
     * @param paisa Signed number of paisa (100 paisa = Rs 1).
     *
     * Marked `explicit` so a bare integer can never silently become money.
     */
    constexpr explicit Money(std::int64_t paisa) noexcept : m_paisa(paisa) {}

    /**
     * @brief Builds an amount from rupees and (optionally) paisa.
     * @param rupees Whole rupees.
     * @param paisa  Additional paisa; defaults to 0 for whole-rupee amounts.
     * @return The combined amount.
     */
    /// @oop-concept Default Arguments :: paisa part defaults to 0 for whole-rupee amounts
    static constexpr Money fromRupees(std::int64_t rupees, std::int64_t paisa = 0) noexcept {
        return Money(rupees * 100 + paisa);
    }

    /**
     * @brief The additive identity, Rs 0.00.
     * @return A zero amount.
     */
    static constexpr Money zero() noexcept { return Money(); }

    /**
     * @brief Raw paisa representation.
     * @return Signed paisa count.
     */
    /// @oop-concept Constant Member Functions :: all observers are const
    constexpr std::int64_t paisa() const noexcept { return m_paisa; }

    /**
     * @brief Whole-rupee part of the amount (truncated toward zero).
     * @return Rupees, discarding the paisa remainder.
     */
    constexpr std::int64_t wholeRupees() const noexcept { return m_paisa / 100; }

    /**
     * @brief Tests for exactly zero.
     * @return true when the amount is Rs 0.00.
     */
    constexpr bool isZero() const noexcept { return m_paisa == 0; }

    /**
     * @brief Tests for a debit / negative amount.
     * @return true when the amount is below zero.
     */
    constexpr bool isNegative() const noexcept { return m_paisa < 0; }

    /**
     * @brief Formats for display, e.g. `Rs 1,250.00`.
     * @return Thousands-grouped text with exactly two decimal places, prefixed `Rs `.
     *
     * Negative amounts render as `-Rs 1,250.00`. Implemented in `Money.cpp`.
     */
    QString toString() const;

    /**
     * @brief Computes a percentage of this amount, rounded half-up.
     * @param pct Percentage to take (e.g. 10 for 10 %). Callers validate the range.
     * @return `pct`% of this amount, rounded half-up away from zero.
     *
     * Used by discount and service-charge calculation. Implemented in `Money.cpp`.
     */
    Money percent(int pct) const noexcept;

    /**
     * @brief Adds another amount in place.
     * @param rhs Amount to add.
     * @return Reference to this object, so assignments chain.
     */
    /// @oop-concept Return by Reference :: compound assignment returns *this for chaining
    Money& operator+=(Money rhs) noexcept {
        m_paisa += rhs.m_paisa;
        return *this;
    }

    /**
     * @brief Subtracts another amount in place.
     * @param rhs Amount to subtract.
     * @return Reference to this object.
     */
    Money& operator-=(Money rhs) noexcept {
        m_paisa -= rhs.m_paisa;
        return *this;
    }

    /**
     * @brief Scales this amount by an integer factor (e.g. a line quantity).
     * @param factor Integer multiplier.
     * @return Reference to this object.
     */
    Money& operator*=(std::int64_t factor) noexcept {
        m_paisa *= factor;
        return *this;
    }

    /**
     * @brief Writes the amount to a std::ostream as `NPR <rupees>.<paisa>`.
     * @param os Destination stream.
     * @param m  Amount to write.
     * @return The same stream, for chaining.
     *
     * Grants access to the private paisa representation so text receipts and
     * CSV rows can be produced without a formatting round-trip through QString.
     */
    /// @oop-concept Friend Function :: stream insertion needs the raw paisa representation
    friend std::ostream& operator<<(std::ostream& os, const Money& m);

private:
    std::int64_t m_paisa = 0; ///< The entire state of the type: signed paisa.
};

/// @brief Namespace-scope declaration of the stream inserter defined in `Money.cpp`.
std::ostream& operator<<(std::ostream& os, const Money& m);

/**
 * @brief Adds two amounts.
 * @param a Left operand.
 * @param b Right operand.
 * @return The sum.
 */
constexpr Money operator+(Money a, Money b) noexcept { return Money(a.paisa() + b.paisa()); }

/**
 * @brief Subtracts one amount from another.
 * @param a Minuend.
 * @param b Subtrahend.
 * @return The difference (may be negative, e.g. change due).
 */
constexpr Money operator-(Money a, Money b) noexcept { return Money(a.paisa() - b.paisa()); }

/**
 * @brief Negates an amount (unary minus) — used for refunds and credits.
 * @param a Amount to negate.
 * @return The negated amount.
 */
constexpr Money operator-(Money a) noexcept { return Money(-a.paisa()); }

/**
 * @brief Scales an amount by an integer quantity.
 * @param a Unit amount.
 * @param q Quantity.
 * @return The line total.
 */
constexpr Money operator*(Money a, std::int64_t q) noexcept { return Money(a.paisa() * q); }

/**
 * @brief Scales an amount by an integer quantity (commuted form).
 * @param q Quantity.
 * @param a Unit amount.
 * @return The line total.
 */
/// @oop-concept Function Overloading :: the same operator accepts either operand order
constexpr Money operator*(std::int64_t q, Money a) noexcept { return a * q; }

/**
 * @brief Equality comparison.
 * @param a Left operand.
 * @param b Right operand.
 * @return true when both amounts hold the same paisa.
 */
constexpr bool operator==(Money a, Money b) noexcept { return a.paisa() == b.paisa(); }

/**
 * @brief Inequality comparison.
 * @param a Left operand.
 * @param b Right operand.
 * @return true when the amounts differ.
 */
constexpr bool operator!=(Money a, Money b) noexcept { return !(a == b); }

/**
 * @brief Strict less-than ordering (lets Money be sorted and used as a map key).
 * @param a Left operand.
 * @param b Right operand.
 * @return true when @p a is smaller than @p b.
 */
constexpr bool operator<(Money a, Money b) noexcept { return a.paisa() < b.paisa(); }

/**
 * @brief Less-than-or-equal ordering.
 * @param a Left operand.
 * @param b Right operand.
 * @return true when @p a does not exceed @p b.
 */
constexpr bool operator<=(Money a, Money b) noexcept { return a.paisa() <= b.paisa(); }

/**
 * @brief Greater-than ordering, expressed through operator<.
 * @param a Left operand.
 * @param b Right operand.
 * @return true when @p a exceeds @p b.
 */
constexpr bool operator>(Money a, Money b) noexcept { return b < a; }

/**
 * @brief Greater-than-or-equal ordering, expressed through operator<=.
 * @param a Left operand.
 * @param b Right operand.
 * @return true when @p a is at least @p b.
 */
constexpr bool operator>=(Money a, Money b) noexcept { return b <= a; }

/**
 * @brief Formats an amount for any Qt view, label or receipt line.
 * @param m Amount to format.
 * @return The same text as Money::toString().
 */
/// @oop-concept Inline Functions :: explicit inline free helper used at every presentation edge
inline QString formatNpr(Money m) { return m.toString(); }

} // namespace aluchop::core

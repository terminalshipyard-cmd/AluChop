/// \file
/// \brief Implementation of the NPR money value type.
///
/// Three members live here rather than in the header, because each of them
/// needs machinery the header deliberately keeps out of every translation unit
/// that merely *holds* an amount: QString formatting, integer half-up rounding
/// and `<ostream>`.
///
/// Not one line of this file uses a floating-point type. Percentages are taken
/// with integer arithmetic and rounded half-up away from zero, so a 10 %
/// discount on Rs 12.35 is exactly Rs 1.24 — and stays exactly Rs 1.24 no
/// matter how many times the bill is recomputed.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/core/Money.hpp"

#include <cstdint>
#include <ostream>

#include <QChar>
#include <QLatin1Char>
#include <QString>

namespace aluchop::core {
namespace {

/// \brief Absolute value of a paisa count as an unsigned magnitude.
///
/// Written this way rather than as `-v` because negating the most negative
/// `std::int64_t` is undefined behaviour; complementing the unsigned bit
/// pattern is well defined for every input.
/// \param v Signed paisa count.
/// \return |v| as an unsigned 64-bit magnitude.
std::uint64_t magnitudeOf(std::int64_t v) noexcept
{
    const auto bits = static_cast<std::uint64_t>(v);
    return v < 0 ? (~bits + 1u) : bits;
}

/// \brief Inserts thousands separators into a plain digit string.
/// \param digits Digits only, no sign and no separators (e.g. `"1250"`).
/// \return The same number grouped in threes from the right (e.g. `"1,250"`).
QString groupThousands(const QString& digits)
{
    QString grouped = digits;
    for (int cut = grouped.size() - 3; cut > 0; cut -= 3)
        grouped.insert(cut, QLatin1Char(','));
    return grouped;
}

} // namespace

/// @oop-concept Constant Member Functions :: formatting only observes the amount
QString Money::toString() const
{
    const std::uint64_t magnitude = magnitudeOf(m_paisa);
    const std::uint64_t rupees = magnitude / 100u;
    const std::uint64_t paisaPart = magnitude % 100u;

    QString text;
    if (m_paisa < 0)
        text += QLatin1Char('-');
    text += QStringLiteral("Rs ");
    text += groupThousands(QString::number(static_cast<qulonglong>(rupees)));
    text += QLatin1Char('.');
    text += QString::number(static_cast<qulonglong>(paisaPart)).rightJustified(2, QLatin1Char('0'));
    return text;
}

/// @oop-concept Constants :: 100 paisa to the rupee is the only magic number here, and
/// it is the definition of the unit rather than a tunable
Money Money::percent(int pct) const noexcept
{
    // Exact integer half-up rounding, away from zero, with no intermediate
    // floating-point value: (|paisa| * pct + 50) / 100, sign reapplied after.
    const std::int64_t scaled = m_paisa * static_cast<std::int64_t>(pct);
    const std::int64_t rounded =
        scaled >= 0 ? (scaled + 50) / 100 : -((-scaled + 50) / 100);
    return Money(rounded);
}

/// @oop-concept Friend Function :: stream insertion is not a member (the stream is the left
/// operand), yet it needs the private paisa field — exactly what `friend` is for
std::ostream& operator<<(std::ostream& os, const Money& m)
{
    // The friend declaration exists precisely so this can read m_paisa and emit
    // a locale-independent form for receipts, CSV rows and audit details.
    const std::uint64_t magnitude = magnitudeOf(m.m_paisa);
    if (m.m_paisa < 0)
        os << '-';
    os << "NPR " << static_cast<unsigned long long>(magnitude / 100u) << '.';
    const auto paisaPart = static_cast<unsigned long long>(magnitude % 100u);
    if (paisaPart < 10u)
        os << '0';
    os << paisaPart;
    return os;
}

} // namespace aluchop::core

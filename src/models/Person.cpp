/// \file
/// \brief Implementation of Person — the abstract root and **virtual base** of
///        the Employee/Customer diamond.
///
/// Person carries no behaviour beyond identity, but it carries all of the
/// *validation* of identity. That matters more than it looks: because Person is
/// a virtual base, a StaffCustomer has exactly one of these subobjects, so these
/// four fields and these three rules are the single point at which a name, a
/// phone number or an e-mail address enters the system — no matter which branch
/// of the hierarchy the caller thought it was talking to.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Person.hpp"

#include <string>

#include <QChar>
#include <QString>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::models {
namespace {

/// \brief Shortest accepted run of digits in a phone number.
constexpr int kMinPhoneDigits = 7;

/// \brief Longest accepted run of digits in a phone number (E.164 upper bound).
constexpr int kMaxPhoneDigits = 15;

/// \brief Whether a character is legal punctuation inside a phone number.
/// \param c Character under test.
/// \return true for the separators people actually type: `+ - ( ) space`.
bool isPhonePunctuation(QChar c)
{
    return c == QLatin1Char('+') || c == QLatin1Char('-') || c == QLatin1Char('(')
        || c == QLatin1Char(')') || c == QLatin1Char(' ');
}

} // namespace

/// @oop-concept Parameterised Constructor :: identity arrives validated, never half-built
Person::Person(int id, QString name, QString phone, QString email)
    : m_id(id)
{
    // Routed through the setters on purpose: construction and later editing then
    // enforce exactly the same rules, and a Person can never exist in a state
    // that setName() would have rejected.
    setName(name);
    setPhone(phone);
    setEmail(email);
}

/// @oop-concept Virtual Functions :: a usable default that StaffCustomer refines
QString Person::displayLabel() const
{
    // roleName() is pure virtual, so this call always lands on the concrete
    // role — the base class describes itself using behaviour it does not have.
    return QStringLiteral("%1 (%2)").arg(m_name, roleName());
}

/// @oop-concept Pass by Reference :: the candidate string is inspected, never copied in
void Person::setName(const QString& v)
{
    const QString trimmed = v.trimmed();
    if (trimmed.isEmpty())
        throw core::ValidationException("name must not be blank");
    m_name = trimmed;
}

void Person::setPhone(const QString& v)
{
    const QString trimmed = v.trimmed();
    if (trimmed.isEmpty()) { // optional field
        m_phone.clear();
        return;
    }

    int digits = 0;
    for (const QChar c : trimmed) {
        if (c.isDigit())
            ++digits;
        else if (!isPhonePunctuation(c))
            throw core::ValidationException("phone number contains an invalid character: '"
                                            + trimmed.toStdString() + "'");
    }

    if (digits < kMinPhoneDigits || digits > kMaxPhoneDigits)
        throw core::ValidationException("phone number must contain 7 to 15 digits, got "
                                        + std::to_string(digits));

    m_phone = trimmed;
}

void Person::setEmail(const QString& v)
{
    const QString trimmed = v.trimmed();
    if (trimmed.isEmpty()) { // optional field
        m_email.clear();
        return;
    }

    const int at = trimmed.indexOf(QLatin1Char('@'));
    if (at <= 0 || at == trimmed.size() - 1)
        throw core::ValidationException("e-mail address is not valid: '" + trimmed.toStdString() + "'");

    m_email = trimmed;
}

} // namespace aluchop::models

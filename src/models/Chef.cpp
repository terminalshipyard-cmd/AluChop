/// \file
/// \brief Implementation of Chef — kitchen staff paid a salary plus overtime.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Chef.hpp"

#include <cstdint>
#include <string>
#include <utility>

#include <QString>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Money.hpp"

namespace aluchop::models {

/// @oop-concept Constant Objects / Static Members :: one immutable house rate shared by
/// every Chef object, defined exactly once here rather than repeated in payroll code
const core::Money Chef::kOvertimeRatePerHour = core::Money::fromRupees(300);

/// @oop-concept Default Arguments :: most chefs have no recorded specialty
Chef::Chef(int id, QString name, QString phone, QString email,
           core::Money monthlySalary, QString shift, QString specialty)
    // Person is a virtual base of Employee, so Chef constructs it directly.
    : Person(id, name, phone, email),
      Employee(id, name, phone, email, QStringLiteral("CHEF"), monthlySalary, std::move(shift)),
      m_specialty(specialty.trimmed())
{
}

QString Chef::roleName() const
{
    return QStringLiteral("Chef");
}

/// @oop-concept Method Overriding :: chef pay = base salary + overtime at the house rate
core::Money Chef::monthlyPay() const
{
    // Money * integer quantity: the rate is scaled by whole hours, so the result
    // is exact paisa with no rounding step at all.
    return salary() + kOvertimeRatePerHour * static_cast<std::int64_t>(m_overtimeHours);
}

void Chef::setOvertimeHours(int h)
{
    if (h < 0)
        throw core::ValidationException("overtime hours cannot be negative ("
                                        + std::to_string(h) + ")");
    m_overtimeHours = h;
}

} // namespace aluchop::models

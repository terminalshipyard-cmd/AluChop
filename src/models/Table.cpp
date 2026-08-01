/// \file
/// \brief Implementation of models::Table.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/Table.hpp"

#include <string>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::models {

Table::Table(int id, QString name, int capacity)
    : m_id(id)
{
    // Delegating the field work to the setters (rather than writing the members
    // directly) is what guarantees a Table can never exist in an invalid state,
    // not even for the duration of a constructor body.
    setName(name);
    setCapacity(capacity);
}

void Table::setName(const QString& v)
{
    const QString trimmed = v.trimmed();
    if (trimmed.isEmpty())
        throw core::ValidationException("Table name must not be blank");
    m_name = trimmed;
}

void Table::setCapacity(int c)
{
    if (c < 1)
        throw core::ValidationException(
            "Table capacity must be at least 1 seat (got " + std::to_string(c) + ")");
    m_capacity = c;
}

} // namespace aluchop::models

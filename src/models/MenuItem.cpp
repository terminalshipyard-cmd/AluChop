/// \file
/// \brief Implementation of models::MenuItem.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/models/MenuItem.hpp"

#include <cstdint>

#include <QJsonValue>
#include <QLatin1String>
#include <QStringList>

#include "aluchop/core/AppInfo.hpp"
#include "aluchop/core/Exceptions.hpp"

namespace aluchop::models {
namespace {

/// \brief Resolve \p raw against the 14 canonical categories.
/// \return The canonical spelling, or an empty string when \p raw is not one of them.
///
/// Matching is case-insensitive but *storage* is canonical, so a seed file that
/// says "dessert" and a dialog that says "Dessert" can never produce two menu
/// sections that look identical to the eye and different to a GROUP BY.
QString canonicalCategory(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    for (const char* known : core::kMenuCategories) {
        const QString candidate = QString::fromUtf8(known);
        if (trimmed.compare(candidate, Qt::CaseInsensitive) == 0)
            return candidate;
    }
    return QString();
}

/// \brief List the legal categories for an error message the operator can act on.
QString knownCategoryList()
{
    QStringList names;
    names.reserve(static_cast<int>(core::kMenuCategories.size()));
    for (const char* known : core::kMenuCategories)
        names << QString::fromUtf8(known);
    return names.join(QLatin1String(", "));
}

/// \brief Fetch a required JSON value, or fail loudly.
/// \throws core::ValidationException when \p key is absent or null.
QJsonValue requiredValue(const QJsonObject& obj, const char* key)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isUndefined() || v.isNull())
        throw core::ValidationException(
            std::string("Menu seed entry is missing required key '") + key + "'");
    return v;
}

} // namespace

MenuItem::MenuItem(int id, QString name, QString category, core::Money price, QString description)
    : m_id(id)
    , m_description(description.trimmed())
{
    // Routed through the setters so an invalid dish cannot exist even briefly.
    setName(name);
    setCategory(category);
    setPrice(price);
}

void MenuItem::setName(const QString& v)
{
    const QString trimmed = v.trimmed();
    if (trimmed.isEmpty())
        throw core::ValidationException("Menu item name must not be blank");
    m_name = trimmed;
}

void MenuItem::setCategory(const QString& v)
{
    const QString canonical = canonicalCategory(v);
    if (canonical.isEmpty())
        throw core::ValidationException(
            "Unknown menu category '" + v.trimmed().toStdString() + "'; expected one of: "
            + knownCategoryList().toStdString());
    m_category = canonical;
}

void MenuItem::setPrice(core::Money p)
{
    // The price is tax-INCLUSIVE, exactly as printed on the physical menu.
    // Nothing in this application ever adds tax on top of it.
    if (p.isNegative())
        throw core::ValidationException("Menu item price must not be negative");
    m_price = p;
}

QJsonObject MenuItem::toJson() const
{
    QJsonObject obj;
    obj.insert(QLatin1String("name"), m_name);
    obj.insert(QLatin1String("category"), m_category);
    // Money crosses the JSON boundary as integer paisa — never as a double, which
    // is the whole reason core::Money exists.
    obj.insert(QLatin1String("price_paisa"), static_cast<qint64>(m_price.paisa()));
    obj.insert(QLatin1String("description"), m_description);
    obj.insert(QLatin1String("image"), m_imagePath);
    obj.insert(QLatin1String("available"), m_available);
    return obj;
}

void MenuItem::fromJson(const QJsonObject& obj)
{
    // Required keys — a seed row without them is corrupt and must fail loudly at
    // load time rather than become a nameless, free dish in the till.
    setName(requiredValue(obj, "name").toString());
    setCategory(requiredValue(obj, "category").toString());

    const QJsonValue priceValue = requiredValue(obj, "price_paisa");
    if (!priceValue.isDouble())
        throw core::ValidationException("Menu seed key 'price_paisa' must be an integer paisa count");
    setPrice(core::Money(static_cast<std::int64_t>(priceValue.toInteger())));

    // Optional keys. The seed file writes each of these under two spellings for
    // compatibility, so both are accepted and the richer one wins.
    m_description = obj.value(QLatin1String("description")).toString().trimmed();

    QString image = obj.value(QLatin1String("image")).toString();
    if (image.isEmpty())
        image = obj.value(QLatin1String("image_path")).toString();
    m_imagePath = image.trimmed();

    if (obj.contains(QLatin1String("available")))
        m_available = obj.value(QLatin1String("available")).toBool(true);
    else
        m_available = obj.value(QLatin1String("is_available")).toBool(true);

    // The primary key is assigned by the database on INSERT, so it is only read
    // back when the document actually carries one (a re-import of an export).
    const QJsonValue idValue = obj.value(QLatin1String("id"));
    if (idValue.isDouble())
        m_id = idValue.toInt(m_id);
}

bool MenuItem::operator<(const MenuItem& rhs) const
{
    const int byName = m_name.compare(rhs.m_name, Qt::CaseInsensitive);
    if (byName != 0)
        return byName < 0;
    // Two dishes may legitimately share a name across categories ("House Salad"
    // as a starter and as a side). Falling back to the identity keeps this a
    // strict weak ordering, which std::sort requires.
    if (m_category != rhs.m_category)
        return m_category < rhs.m_category;
    return m_id < rhs.m_id;
}

} // namespace aluchop::models

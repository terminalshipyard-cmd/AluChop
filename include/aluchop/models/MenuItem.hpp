#pragma once

/// \file
/// \brief MenuItem — one dish or drink on the restaurant menu.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QJsonObject>
#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/models/Interfaces.hpp"

namespace aluchop::models {

/// @oop-concept Multiple Classes / Encapsulation :: every field is private behind a
/// validating setter, so an invalid dish cannot exist even briefly
/// \brief A priced, categorised, optionally-available menu entry.
///
/// MenuItem implements ISerializable because the menu is *seeded from JSON*:
/// `assets/menu/menu_seed.json` is read by SchemaMigrator on first run and each
/// element is handed straight to fromJson(). Serialisation is therefore a real
/// requirement of the class, not an add-on.
///
/// \warning The stored price is **tax-inclusive**, as printed on the physical
///          menu. No layer of this application ever adds tax on top of it.
class MenuItem : public ISerializable {
public:
    /// \brief Default constructor — used by MenuRepository before hydration.
    MenuItem() = default;

    /// @oop-concept Parameterised Constructor :: a dish is meaningless without name, category and price
    /// \param id          Database primary key, 0 when not yet persisted.
    /// \param name        Dish name; must not be blank.
    /// \param category    One of the 14 categories in core::kMenuCategories.
    /// \param price       Tax-inclusive price in NPR; must not be negative.
    /// \param description Menu blurb.
    /// \throws core::ValidationException when a field fails its setter rule.
    MenuItem(int id, QString name, QString category, core::Money price, QString description);

    /// \brief Database primary key.
    int id() const noexcept { return m_id; }
    /// \brief Assign the primary key after an INSERT.
    void setId(int v) { m_id = v; }

    /// \brief Dish name as printed on the menu.
    const QString& name() const noexcept { return m_name; }
    /// \brief Set the dish name.
    /// \throws core::ValidationException when \p v trims to empty.
    void setName(const QString& v);

    /// \brief Menu category, e.g. "From the Tandoor".
    const QString& category() const noexcept { return m_category; }
    /// \brief Set the menu category.
    /// \throws core::ValidationException when \p v is not one of
    ///         core::kMenuCategories — the single source of truth for the 14
    ///         categories, so a typo can never create a fifteenth section.
    void setCategory(const QString& v);

    /// \brief Tax-inclusive price in NPR.
    core::Money price() const noexcept { return m_price; }
    /// \brief Set the tax-inclusive price.
    /// \throws core::ValidationException when \p p is negative.
    void setPrice(core::Money p);

    /// \brief Menu blurb shown under the name.
    const QString& description() const noexcept { return m_description; }
    /// \brief Set the menu blurb.
    void setDescription(const QString& v) { m_description = v; }

    /// \brief Path to the dish photograph; empty when there is none.
    const QString& imagePath() const noexcept { return m_imagePath; }
    /// \brief Set the photograph path.
    void setImagePath(const QString& v) { m_imagePath = v; }

    /// \brief Whether the kitchen is currently serving this item.
    bool isAvailable() const noexcept { return m_available; }
    /// \brief Toggle availability (undoable from MenuPage via ToggleAvailabilityCommand).
    void setAvailable(bool a) { m_available = a; }

    /// \brief Serialise to the seed-file shape.
    /// \return Keys: `name`, `category`, `price_paisa`, `description`, `image`, `available`.
    QJsonObject toJson() const override;

    /// \brief Rebuild from the seed-file shape; used by SchemaMigrator on first run.
    /// \param obj One element of the menu seed array.
    /// \throws core::ValidationException when a key is missing or a value is invalid.
    void fromJson(const QJsonObject& obj) override;

    /// @oop-concept Friend Function :: symmetric equality written as a free function so
    /// neither operand is privileged, yet it still reads the private key directly
    /// \brief Two menu items are the same item when they are the same database row.
    ///
    /// Identity is the id, not the name: renaming "Chicken Momo" to "Chicken
    /// Mo:Mo" must not turn it into a different dish in an open order.
    friend bool operator==(const MenuItem& a, const MenuItem& b) noexcept {
        return a.m_id == b.m_id;
    }

    /// \brief Negation of operator==.
    friend bool operator!=(const MenuItem& a, const MenuItem& b) noexcept {
        return !(a == b);
    }

    /// @oop-concept Relational Operator Overloading :: gives std::sort a default order
    /// \brief Case-insensitive ordering by name — the default menu sort.
    /// \param rhs Item to compare against.
    /// \return `true` when this item sorts before \p rhs.
    bool operator<(const MenuItem& rhs) const;

private:
    int m_id = 0;           ///< Primary key.
    QString m_name;         ///< Dish name.
    QString m_category;     ///< One of core::kMenuCategories.
    QString m_description;  ///< Menu blurb.
    QString m_imagePath;    ///< Photograph path; may be empty.
    core::Money m_price;    ///< Tax-inclusive price, in paisa.
    bool m_available = true;///< Whether the kitchen is serving it now.
};

} // namespace aluchop::models

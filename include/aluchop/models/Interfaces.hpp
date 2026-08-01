#pragma once

/// \file
/// \brief The four capability mixins of the domain model.
///
/// Each interface is a *pure* abstract class: no data members, no constructors,
/// only a virtual destructor and pure virtual functions. Domain classes inherit
/// them publicly to advertise a capability (`Bill` is printable **and**
/// discountable; `MenuItem` serialises itself; `Admin` describes itself for the
/// audit trail). Because they carry no state they can be combined freely without
/// creating a diamond of data.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <QString>
#include <QJsonObject>

#include "aluchop/core/Money.hpp"

namespace aluchop::models {

/// @oop-concept Abstract Classes / Pure Virtual Functions :: capability mixins with no state
/// \brief Anything that can render itself as monospace text for a receipt,
///        kitchen ticket or plain-text export.
///
/// Implemented by Order (kitchen ticket) and Bill (customer receipt). The GUI's
/// PdfExporter and the raw text-file layer both consume this single method, so
/// neither of them needs to know the concrete type.
class IPrintable {
public:
    /// @oop-concept Virtual Destructor :: interfaces are always deleted through the base pointer
    virtual ~IPrintable() = default;

    /// \brief Render the object as a fixed-width text block.
    /// \return Newline-separated body, ready to drop into a receipt or QPdfWriter.
    virtual QString toPrintableText() const = 0;
};

/// \brief Anything that round-trips through JSON.
///
/// Implemented by MenuItem so that `assets/menu/menu_seed.json` can be read by
/// SchemaMigrator on first run, and so that a menu can be exported again later.
class ISerializable {
public:
    virtual ~ISerializable() = default;

    /// \brief Serialise the object's persistent state.
    /// \return A JSON object containing every field needed to rebuild it.
    virtual QJsonObject toJson() const = 0;

    /// \brief Rebuild the object's state from JSON.
    /// \param obj Source object, normally one element of the menu seed array.
    /// \throws core::ValidationException when a required key is missing or a
    ///         value fails the same rules the setters enforce.
    virtual void fromJson(const QJsonObject& obj) = 0;
};

/// \brief Anything that can describe itself in one line for the audit trail.
///
/// Implemented by Admin: privileged actions must record *who* performed them in
/// a form that survives in a fixed-width binary record.
class IAuditable {
public:
    virtual ~IAuditable() = default;

    /// \brief One-line, human-readable identity for the audit log.
    /// \return e.g. `"Admin Shashank Bhattarai (id 1)"`.
    virtual QString auditDescription() const = 0;
};

/// \brief Anything a discount can be applied to.
///
/// Implemented by Bill. Keeping this separate from IPrintable is what makes
/// `Bill : public IPrintable, public IDiscountable` an honest use of multiple
/// inheritance: printing and discounting are genuinely independent capabilities.
class IDiscountable {
public:
    virtual ~IDiscountable() = default;

    /// \brief Apply a discount.
    /// \param amount Absolute amount to take off (never a percentage).
    /// \param label  Human-readable reason, shown on the receipt.
    /// \throws core::ValidationException when \p amount is negative or exceeds
    ///         the discountable base.
    virtual void setDiscount(core::Money amount, const QString& label) = 0;

    /// \brief The discount currently applied.
    virtual core::Money discount() const = 0;
};

} // namespace aluchop::models

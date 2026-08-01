#pragma once

/// \file
/// \brief Order — the central aggregate of the system: a sequence of line items
///        moving through the kitchen pipeline.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include <cstddef>
#include <vector>

#include <QDateTime>
#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/Interfaces.hpp"
#include "aluchop/models/OrderItem.hpp"

namespace aluchop::models {

/// \brief Everything a guest has asked for, in one place.
///
/// Order owns its line items by value (`std::vector<OrderItem>`), which is what
/// makes splitting and merging bills expressible as ordinary C++ value
/// operations rather than as database gymnastics:
///
/// - **Split**  — copy the order (copy constructor), move the chosen lines into
///                the copy, persist the copy as a new order.
/// - **Merge**  — `target += source` (Order::operator+=), then cancel the source.
///
/// It implements IPrintable so the kitchen can print a ticket straight from the
/// object without the GUI reaching into its internals.
///
/// \warning All prices held here are tax-inclusive. subtotal() sums line totals
///          and adds nothing.
class Order : public IPrintable {
public:
    /// @oop-concept Constructor :: every construction registers the order in the live counter
    /// \brief Create an empty, unsaved dine-in order in status Open.
    Order();

    /// @oop-concept Parameterised Constructor :: hydrate an existing order header
    /// \param id         Database primary key.
    /// \param orderNumber Human-facing number, `ORD-YYYYMMDD-NNN`.
    /// \param type       Dine-in, takeaway or delivery.
    /// \param tableId    Table for a dine-in order, otherwise 0.
    /// \param customerId Loyalty customer, or 0 for a walk-in.
    /// \param waiterId   Employee who opened it, or 0.
    /// \param createdAt  Creation timestamp (UTC).
    Order(int id, QString orderNumber, OrderType type,
          int tableId, int customerId, int waiterId, QDateTime createdAt);

    /// @oop-concept Copy Constructor :: split-bill genuinely copies an order —
    /// the copy gets id 0 / empty number (a new, unsaved order) and its OWN item vector
    /// \brief Deep-copy the order as a **new, unsaved** order.
    ///
    /// Copying is how a split begins, so a copy is never the same order twice:
    /// the id and order number are reset, because two rows may not share a
    /// primary key or a unique order number. Everything else — type, table,
    /// customer, waiter, status and a private copy of the item vector — carries
    /// over.
    /// \param other Order to copy from; left untouched.
    Order(const Order& other);

    /// @oop-concept Assignment Operator :: same deep-copy semantics as the copy ctor
    /// \brief Deep-copy assignment, self-assignment safe; also resets id and number.
    /// \param other Order to copy from.
    /// \return `*this`, so assignments chain like the built-in operator.
    Order& operator=(const Order& other);

    /// @oop-concept Rule of Five / Move Semantics :: a *move* is not a split — it must
    /// preserve identity, because that is what `std::vector<Order>` and
    /// `std::optional<Order>` do when a repository hydrates rows out of the database.
    /// \brief Steal \p other's state, identity included.
    ///
    /// Declared `noexcept` deliberately: `std::vector` only relocates with a move
    /// constructor when it is `noexcept`, and falling back to the *copy*
    /// constructor here would silently reset the id of every order loaded from
    /// SQLite.
    /// \param other Order to move from; left empty and safely destructible.
    Order(Order&& other) noexcept;

    /// \brief Move assignment; preserves identity, `noexcept`.
    /// \param other Order to move from.
    /// \return `*this`.
    Order& operator=(Order&& other) noexcept;

    /// @oop-concept Destructor :: maintains the live open-order counter
    /// \brief Deregister this order from the live-object counter.
    ///
    /// `override` is not decoration: IPrintable declares a virtual destructor, so
    /// this one overrides it and orders held as `IPrintable*` are destroyed correctly.
    ~Order() override;

    // --- items -------------------------------------------------------------

    /// @oop-concept Function Overloading :: add a prepared line OR build one in place
    /// \brief Append a prepared line, merging quantities when the dish is already on the order.
    /// \param item Line to add.
    void addItem(const OrderItem& item);

    /// \brief Build and append a line in place, merging quantities like the overload above.
    /// \param menuItemId Source menu row.
    /// \param name       Dish-name snapshot.
    /// \param unitPrice  Tax-inclusive unit-price snapshot.
    /// \param qty        Quantity; must be at least 1.
    /// \throws core::ValidationException when \p qty is below 1.
    void addItem(int menuItemId, const QString& name, core::Money unitPrice, int qty);

    /// \brief Remove the line at \p index.
    /// \param index Zero-based position.
    /// \throws std::out_of_range when \p index is past the end.
    void removeItemAt(std::size_t index);

    /// \brief Number of distinct lines (not the total quantity).
    std::size_t itemCount() const noexcept { return m_items.size(); }

    /// @oop-concept Return by Reference :: the whole line vector is observed without a copy
    /// \brief Read-only view of the lines.
    const std::vector<OrderItem>& items() const noexcept { return m_items; }

    /// @oop-concept Subscript Operator / Return by Reference :: an order IS a sequence of lines
    /// \brief Mutable access to a line, so `order[2].setQty(3)` reads naturally.
    /// \param i Zero-based position.
    /// \return Reference to the live line, not a copy.
    /// \throws std::out_of_range when \p i is past the end — unlike the raw
    ///         `std::vector` subscript, this one is checked, because the index
    ///         comes from a table selection in the GUI.
    OrderItem& operator[](std::size_t i);

    /// @oop-concept Const Overloading :: the same subscript on a `const Order` yields a const line
    /// \brief Read-only access to a line.
    /// \param i Zero-based position.
    /// \throws std::out_of_range when \p i is past the end.
    const OrderItem& operator[](std::size_t i) const;

    /// @oop-concept Operator Overloading :: merging bills is the domain meaning of +=
    /// \brief Absorb another order's lines into this one.
    ///
    /// Quantities of the same dish are combined rather than duplicated, exactly
    /// as addItem() does. Only the items move; this order keeps its own id,
    /// number, table and status.
    /// \param other Order whose lines are absorbed; left unchanged.
    /// \return `*this`, so merges chain.
    Order& operator+=(const Order& other);

    /// \brief Sum of every line total.
    /// \return Computed through core::sumMoney(); tax-inclusive, nothing added.
    core::Money subtotal() const;

    // --- lifecycle ---------------------------------------------------------

    /// \brief Where the order sits in the kitchen pipeline.
    OrderStatus status() const noexcept { return m_status; }

    /// \brief Advance (or cancel) the order.
    ///
    /// Legal transitions — anything else is rejected:
    /// `Open → {Pending, Cancelled}`, `Pending → {Preparing, Cancelled}`,
    /// `Preparing → Ready`, `Ready → Served`, `Served → Paid`.
    /// Enforcing this in the model, rather than in the GUI, is what stops an
    /// order being marked Paid straight from Open by any code path at all.
    /// \param s Requested new status.
    /// \throws core::ValidationException when the transition is not legal.
    void setStatus(OrderStatus s);

    // --- identity / metadata ----------------------------------------------

    /// \brief Database primary key; 0 for an unsaved order.
    int id() const noexcept { return m_id; }
    /// \brief Assign the primary key after an INSERT.
    void setId(int v) { m_id = v; }

    /// \brief Human-facing number, `ORD-YYYYMMDD-NNN`.
    const QString& orderNumber() const noexcept { return m_orderNumber; }
    /// \brief Assign the order number (OrderRepository, at insert time).
    void setOrderNumber(const QString& v) { m_orderNumber = v; }

    /// \brief Dine-in, takeaway or delivery.
    OrderType type() const noexcept { return m_type; }
    /// \brief Set the fulfilment type.
    void setType(OrderType t) { m_type = t; }

    /// \brief Table for a dine-in order, otherwise 0.
    int tableId() const noexcept { return m_tableId; }
    /// \brief Set the table.
    void setTableId(int v) { m_tableId = v; }

    /// \brief Loyalty customer, or 0 for a walk-in.
    int customerId() const noexcept { return m_customerId; }
    /// \brief Attach a loyalty customer.
    void setCustomerId(int v) { m_customerId = v; }

    /// \brief Employee who opened the order, or 0.
    int waiterId() const noexcept { return m_waiterId; }
    /// \brief Set the owning waiter.
    void setWaiterId(int v) { m_waiterId = v; }

    /// \brief Creation timestamp (UTC).
    QDateTime createdAt() const noexcept { return m_createdAt; }
    /// \brief Set the creation timestamp (repository hydration).
    void setCreatedAt(QDateTime t) { m_createdAt = t; }

    /// \brief Order-wide note, e.g. "birthday — bring candle".
    const QString& note() const noexcept { return m_note; }
    /// \brief Set the order-wide note.
    void setNote(const QString& n) { m_note = n; }

    /// \brief Kitchen ticket: number, table, timestamp and every line with its note.
    QString toPrintableText() const override;

    /// @oop-concept Static Members :: how many Order objects are alive right now (dashboard uses it)
    /// \brief Live Order-object count.
    /// \return The value of the private static counter.
    ///
    /// \par Counter contract (binding)
    /// **Every** constructor — default, parameterised, copy and move — increments
    /// the counter, and the destructor decrements it exactly once. Incrementing
    /// only in the default constructor while always decrementing would drive the
    /// count negative the first time an order is copied.
    static int openOrderCount() noexcept;

private:
    int m_id = 0;                            ///< Primary key; 0 when unsaved.
    QString m_orderNumber;                   ///< `ORD-YYYYMMDD-NNN`.
    OrderType m_type = OrderType::DineIn;    ///< Fulfilment type.
    OrderStatus m_status = OrderStatus::Open;///< Position in the kitchen pipeline.
    int m_tableId = 0;                       ///< Table, or 0.
    int m_customerId = 0;                    ///< Loyalty customer, or 0.
    int m_waiterId = 0;                      ///< Owning waiter, or 0.
    QDateTime m_createdAt;                   ///< Creation timestamp (UTC).
    QString m_note;                          ///< Order-wide note.

    /// @oop-concept Objects as Members :: an order literally contains its line objects,
    /// which is what lets a split move lines between two orders by value
    std::vector<OrderItem> m_items;

    static int s_openCount;                  ///< Live Order objects; defined in Order.cpp.
};

} // namespace aluchop::models

#pragma once

/**
 * @file Commands.hpp
 * @brief Undo/redo for the edits a user is most likely to make by mistake.
 *
 * The command pattern is the honest way to build undo: each reversible edit knows how to do itself
 * and how to take itself back, and `CommandStack` only ever sees the abstract interface. Adding a
 * new undoable action means adding a class, never touching the stack.
 */

#include <cstddef>
#include <memory>
#include <vector>

#include <QString>

#include "aluchop/core/Money.hpp"
#include "aluchop/core/Result.hpp"

namespace aluchop::services {

class OrderService;
class MenuService;
class InventoryService;

/**
 * @brief One reversible edit.
 *
 * /// @oop-concept Pure Virtual Functions / Abstract Classes :: a bare Command means nothing
 * /// @oop-concept Runtime Polymorphism :: CommandStack drives whatever it is handed
 */
class Command {
public:
    virtual ~Command() = default;

    /// @brief Performs the edit.
    /// @throws core::AluChopException subtypes when the edit cannot be applied.
    virtual void execute() = 0;

    /// @brief Reverses the edit performed by execute().
    virtual void undo() = 0;

    /// @return a short human phrase for the Undo/Redo menu entry, e.g. "Add 2 x Momo to ORD-...".
    virtual QString description() const = 0;
};

/// @brief Adds a quantity of a dish to an order.
class AddOrderItemCommand final : public Command {
public:
    AddOrderItemCommand(OrderService& svc, int orderId, int menuItemId, int qty);
    void execute() override;
    void undo() override;
    QString description() const override;
private:
    OrderService& m_svc;
    int m_orderId, m_menuItemId, m_qty;
    std::size_t m_addedIndex = 0;   ///< where the line landed, captured for undo
};

/// @brief Removes a line from an order, remembering enough to put it back.
class RemoveOrderItemCommand final : public Command {
public:
    RemoveOrderItemCommand(OrderService& svc, int orderId, std::size_t index);
    void execute() override;
    void undo() override;
    QString description() const override;
private:
    OrderService& m_svc;
    int m_orderId;
    std::size_t m_index;
    int m_menuItemId = 0, m_qty = 0;   ///< captured at execute() so undo can restore the line
};

/// @brief Switches a menu item on or off.
class ToggleAvailabilityCommand final : public Command {
public:
    ToggleAvailabilityCommand(MenuService& svc, int itemId, bool makeAvailable);
    void execute() override;
    void undo() override;
    QString description() const override;
private:
    MenuService& m_svc;
    int m_itemId;
    bool m_makeAvailable;
};

/// @brief Books stock in; undo books the same quantity back out as an adjustment.
class AdjustStockCommand final : public Command {
public:
    AdjustStockCommand(InventoryService& svc, int ingredientId, double qty,
                       core::Money unitCost, QString note);
    void execute() override;
    void undo() override;
    QString description() const override;
private:
    InventoryService& m_svc;
    int m_ingredientId;
    double m_qty;
    core::Money m_unitCost;
    QString m_note;
};

/**
 * @brief The application-wide undo/redo history.
 *
 * /// @oop-concept STL (vector) + Object Pointers :: two stacks of unique_ptr<Command> hold a
 * /// heterogeneous history; the stack itself never knows which concrete edit it is replaying
 */
class CommandStack {
public:
    /**
     * @brief Executes @p cmd; on success pushes it on the undo stack and clears the redo stack.
     * @return an error carrying the exception message when the command threw.
     * /// @oop-concept try / catch :: the stack converts a thrown edit failure into a Result so the
     * /// GUI can show it without the history ever becoming inconsistent
     */
    core::Result<void> run(std::unique_ptr<Command> cmd);

    /// @return true when there is something to undo.
    bool canUndo() const noexcept { return !m_undo.empty(); }

    /// @return true when there is something to redo.
    bool canRedo() const noexcept { return !m_redo.empty(); }

    /// @return description of the next undo step, or an empty string.
    QString undoText() const;

    /// @return description of the next redo step, or an empty string.
    QString redoText() const;

    /// @brief Reverses the most recent command and moves it to the redo stack.
    core::Result<void> undo();

    /// @brief Re-applies the most recently undone command.
    core::Result<void> redo();

    /// @oop-concept Constants :: the history depth is a named limit, not a magic number
    static constexpr std::size_t kMaxDepth = 50;

private:
    std::vector<std::unique_ptr<Command>> m_undo, m_redo;
};

} // namespace aluchop::services

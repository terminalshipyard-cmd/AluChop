#pragma once

/**
 * @file KitchenQueue.hpp
 * @brief First-in, first-out queue of order ids waiting to be cooked.
 *
 * A real kitchen works in the order tickets arrive, which is exactly the contract `std::queue`
 * offers — so the pass is modelled with one rather than with an ad-hoc vector plus an index.
 * Header-only because it is small, has no dependencies and is used across services and GUI.
 */

#include <cstddef>
#include <queue>
#include <vector>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::services {

/**
 * @brief The kitchen pass.
 *
 * /// @oop-concept STL (queue) :: FIFO of order ids waiting for the kitchen — the container's
 * /// guarantees ARE the domain rule, so no hand-rolled queue is needed
 */
class KitchenQueue {
public:
    /// @brief Puts an order at the back of the pass.
    void push(int orderId) { m_q.push(orderId); }

    /**
     * @brief The next order the kitchen should start.
     * @throws core::ValidationException when the pass is empty — asking for work that is not there
     *         is a programming error, not an empty-value case.
     */
    int front() const {
        if (m_q.empty()) throw core::ValidationException("kitchen queue is empty");
        return m_q.front();
    }

    /// @brief Drops the front order (it has been picked up).
    /// @throws core::ValidationException when the pass is empty.
    void pop() {
        if (m_q.empty()) throw core::ValidationException("kitchen queue is empty");
        m_q.pop();
    }

    /// @return true when nothing is waiting.
    bool empty() const noexcept { return m_q.empty(); }

    /// @return how many tickets are waiting.
    std::size_t size() const noexcept { return m_q.size(); }

    /// @brief Non-destructive view for the kitchen board: copies the queue and drains the copy.
    /// @oop-concept Copy Constructor :: the board copies the queue rather than draining the real
    /// one — showing the pass must never consume it
    std::vector<int> snapshot() const;

    /// @brief Removes one order from anywhere in the queue (the cancel path).
    /// @oop-concept STL Algorithms / Iterators :: the queue is rebuilt without the cancelled id
    void remove(int orderId);

private:
    std::queue<int> m_q;
};

} // namespace aluchop::services

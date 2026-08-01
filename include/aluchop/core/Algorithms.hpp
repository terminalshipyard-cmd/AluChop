#pragma once
/**
 * @file Algorithms.hpp
 * @brief Small generic helpers shared by billing, dashboards, reports and payroll.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * These are function templates rather than overloads because the same three
 * operations are needed over completely different containers and element
 * types: `std::vector<OrderItem>`, `std::vector<Ingredient>`,
 * `std::vector<std::unique_ptr<Employee>>` and so on. Writing them once here
 * is what stops the same loop being copy-pasted into five services.
 *
 * Header-only: templates must be fully visible where they are instantiated.
 */

#include "aluchop/core/Money.hpp"

namespace aluchop::core {

/**
 * @brief Sums a Money field projected out of every element of a container.
 * @tparam Container Any type exposing begin()/end() iterators.
 * @tparam Projection Callable taking an element and returning core::Money.
 * @param c    The container to walk.
 * @param proj Projection applied to each element, e.g. `[](const OrderItem& i){ return i.lineTotal(); }`.
 * @return The total; Money::zero() for an empty container.
 *
 * Uses explicit iterators rather than a range-for to make STL iteration
 * visible in the code, and because it works for any conforming container.
 */
/// @oop-concept Function Template :: one summation used by billing, dashboards and reports alike
template <typename Container, typename Projection>
Money sumMoney(const Container& c, Projection proj) {
    Money total;
    for (auto it = c.begin(); it != c.end(); ++it) // explicit iterators — STL iteration
        total += proj(*it);
    return total;
}

/**
 * @brief Counts the elements of a container that satisfy a predicate.
 * @tparam Container Any range-for iterable type.
 * @tparam Predicate Callable taking an element and returning bool.
 * @param c    The container to walk.
 * @param pred Test applied to each element, e.g. `[](const Ingredient& i){ return i.isLowStock(); }`.
 * @return The number of matching elements.
 *
 * Feeds the dashboard's low-stock and active-order stat cards.
 */
template <typename Container, typename Predicate>
int countMatching(const Container& c, Predicate pred) {
    int n = 0;
    for (const auto& e : c)
        if (pred(e)) ++n;
    return n;
}

/**
 * @brief Constrains a value to an inclusive range using only `operator<`.
 * @tparam T Any less-than-comparable type.
 * @param v  Value to constrain.
 * @param lo Lower bound.
 * @param hi Upper bound.
 * @return const reference to @p lo, @p v or @p hi — whichever is in range.
 *
 * Used by Employee::setPerformanceRating (1..5) and by animation timing.
 * Returning by reference avoids copying larger T and mirrors std::clamp.
 */
/// @oop-concept Return by Reference :: the chosen bound is handed back without a copy
template <typename T>
const T& clampValue(const T& v, const T& lo, const T& hi) {
    return v < lo ? lo : (hi < v ? hi : v);
}

} // namespace aluchop::core

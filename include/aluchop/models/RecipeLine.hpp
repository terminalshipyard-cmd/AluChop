#pragma once

/// \file
/// \brief RecipeLine — how much of one ingredient one serving of one dish consumes.
///
/// \author Shashank Bhattarai (ACE082BCT078)

namespace aluchop::models {

/// @oop-concept Structures :: pure data link between a dish and an ingredient
/// \brief One row of a recipe: dish, ingredient, quantity per serving.
///
/// This is a `struct` rather than a `class` on purpose, and the distinction is
/// the point. Everywhere else in the model layer a field is private behind a
/// validating setter because there is an invariant to protect. A RecipeLine has
/// none: it is three independent numbers whose only meaning is the join between
/// `menu_items` and `ingredients`. Wrapping it in getters would add ceremony and
/// protect nothing.
///
/// InventoryService reads these when an order is served: for each line it
/// subtracts `qtyPerServing × orderedQty` from the ingredient's stock and writes
/// an `inventory_transactions` row with reason "USAGE".
struct RecipeLine {
    int menuItemId = 0;         ///< The dish (`menu_items.id`).
    int ingredientId = 0;       ///< The raw material (`ingredients.id`).
    double qtyPerServing = 0.0; ///< Amount consumed per serving, in the ingredient's unit.
};

} // namespace aluchop::models

#pragma once

/**
 * @file CustomersPage.hpp
 * @brief Screen 4 — customer database, loyalty points, visit history and favourites.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QString>

#include "aluchop/gui/Page.hpp"

class QLabel;
class QLineEdit;
class QTableWidget;

namespace aluchop::gui {

/**
 * @brief Everything the restaurant knows about the people it feeds.
 *
 * Backed solely by services::CustomerService: search, create, edit, deactivate, plus the
 * read-only views — visit history (past orders with dates and totals) and favourite items
 * (most-ordered dishes for the selected customer).
 *
 * Loyalty points are displayed, never computed here: the service awards them when an order is
 * settled, and models::Customer's `operator++` records the visit. The screen is a window onto
 * that model, which is why it stays free of arithmetic.
 *
 * @oop-concept Increment Operator (surfaced) :: the "Visits" column is the value that
 *              models::Customer::operator++ maintains
 */
class CustomersPage final : public Page {
    Q_OBJECT
public:
    /// @param ctx composition root; uses AppContext::customers().
    /// @param parent Qt parent.
    explicit CustomersPage(services::AppContext& ctx, QWidget* parent = nullptr);

    /// @return "Customers".
    QString pageTitle() const override;

    /// Re-runs the current search and reloads the detail panes for the selected customer.
    void refresh() override;

private slots:
    /// Search by name, phone or email through CustomerService::search().
    void onSearch();

    /// Creates a customer after validating name/phone in the service layer.
    void onAdd();

    /// Edits the selected customer.
    void onEdit();

    /// Deletes/deactivates the selected customer; the service refuses if orders reference them.
    void onDelete();

    /// Loads the visit history and favourite items for the selected customer.
    void onShowHistory();

private:
    /// @return the id of the selected customer, or 0 when nothing is selected.
    int selectedCustomerId() const;

    QLineEdit* m_search = nullptr;    ///< SearchBar over name / phone / email.
    QTableWidget* m_table = nullptr;  ///< Name | Phone | Email | Visits | Loyalty points.
    QTableWidget* m_history = nullptr;///< Date | Order no. | Items | Total.
    QLabel* m_favourites = nullptr;   ///< "Chicken Momo ×7 · Americano ×5 · Margherita ×3".
};

} // namespace aluchop::gui

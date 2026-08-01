#pragma once

/**
 * @file DashboardPage.hpp
 * @brief Screen 1 — animated statistics, revenue chart, alerts and pending orders.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QString>

#include <array>

#include "aluchop/gui/Page.hpp"

class QChartView;
class QListWidget;
class QTableWidget;

namespace aluchop::gui {

class StatCard;

/**
 * @brief The landing screen: today's business at a glance.
 *
 * Reads (never writes) through four services — services::ReportService for the money numbers
 * and the weekly series, services::InventoryService for low-stock and expiry alerts,
 * services::ReservationService for today's bookings, services::OrderService for the pending
 * queue. All aggregation lives in those services; this screen only lays results out.
 *
 * Contents (SPEC §3 "Dashboard"):
 *  - four StatCard tiles — Today's Sales, Pending Orders, Customer Count, Low-Stock Items —
 *    faded and lifted in with a staggered animation on every load,
 *  - a seven-day revenue bar chart (QtCharts) built from ReportService::weeklySales(),
 *  - a popular-items list,
 *  - an alerts list merging low stock, expiring stock and today's reservations,
 *  - a table of pending orders with their kitchen status.
 *
 * @oop-concept Method Overriding :: pageTitle()/refresh() give the abstract Page its concrete
 *              dashboard behaviour
 * @oop-concept Object Arrays :: exactly four headline metrics, held in std::array<StatCard*,4>
 */
class DashboardPage final : public Page {
    Q_OBJECT
public:
    /// @param ctx composition root supplying the four services this screen reads.
    /// @param parent Qt parent (the stacked widget reparents it).
    explicit DashboardPage(services::AppContext& ctx, QWidget* parent = nullptr);

    /// @return "Dashboard".
    QString pageTitle() const override;

    /// Re-queries every service, repopulates the cards, chart, lists and table, then replays
    /// the entrance animation.
    void refresh() override;

private:
    /// Staggered QPropertyAnimation fade + 12 px rise across the four cards (0/80/160/240 ms).
    void animateCards();

    std::array<StatCard*, 4> m_cards{};   ///< Today's Sales · Pending Orders · Customers · Low Stock.
    QChartView* m_revenueChart = nullptr; ///< 7-day bar series from ReportService::weeklySales().
    QListWidget* m_alerts = nullptr;      ///< Low stock + expiring + today's reservations.
    QTableWidget* m_pendingOrders = nullptr; ///< Order no. | Type | Table | Status | Total.
    QListWidget* m_popularItems = nullptr;   ///< Best sellers by quantity sold.
};

} // namespace aluchop::gui

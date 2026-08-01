#pragma once

/**
 * @file ReportGenerator.hpp
 * @brief The five exportable reports and the CSV template method they share.
 *
 * A report is a title, a header row and a freshly computed set of rows. Turning that into a file
 * is identical for all five, so `ReportGenerator::exportCsv` is a template method: open, write the
 * header, write every row, close — with the varying parts delegated to virtual functions.
 */

#include <vector>

#include <QDate>
#include <QString>
#include <QStringList>

#include "aluchop/persistence/CsvWriter.hpp"
#include "aluchop/persistence/CustomerRepository.hpp"
#include "aluchop/persistence/EmployeeRepository.hpp"
#include "aluchop/persistence/IngredientRepository.hpp"
#include "aluchop/persistence/OrderRepository.hpp"
#include "aluchop/persistence/PaymentRepository.hpp"

namespace aluchop::services {

/**
 * @brief Abstract base of every report.
 *
 * /// @oop-concept Protected Inheritance :: a report is implemented-in-terms-of a CsvWriter, and
 * /// its DERIVED report classes also need the writer verbs — protected (not private) re-use, while
 * /// outside callers can only call exportCsv()/title()/header()/rows(), never writeRow() directly.
 * /// @oop-concept Abstract Classes :: there is no such thing as a generic report to instantiate
 */
class ReportGenerator : protected persistence::CsvWriter {
public:
    /// The polymorphic deletion point: reports are held as `unique_ptr<ReportGenerator>`.
    /// The protected `CsvWriter` base is never deleted through, so its non-virtual dtor is safe.
    virtual ~ReportGenerator() = default;

    /// @return the human title printed above the table and used in the PDF.
    virtual QString title() const = 0;

    /// @return the column headings, in order.
    /// @oop-concept Pure Virtual Functions :: each report defines its own shape
    virtual QStringList header() const = 0;

    /// @return the body rows, re-queried on every call so an export is never stale.
    virtual std::vector<QStringList> rows() const = 0;

    /**
     * @brief Writes the report to @p outPath as CSV.
     * @return the path written (echoed back for the "saved to..." notification).
     * @throws core::FileIOException from the underlying writer on any IO failure.
     *
     * /// @oop-concept Runtime Polymorphism :: one algorithm here, five different reports produced
     */
    QString exportCsv(const QString& outPath);
};

/// @brief Revenue per day across a date range.
/// @oop-concept Hierarchical Inheritance :: five siblings specialise one abstract report
class SalesReport final : public ReportGenerator {
public:
    SalesReport(const persistence::PaymentRepository& payments, QDate from, QDate to);
    QString title() const override;                  ///< "Sales Report <from>-<to>"
    QStringList header() const override;             ///< Date | Orders Paid | Revenue
    std::vector<QStringList> rows() const override;
private:
    const persistence::PaymentRepository& m_payments;
    QDate m_from, m_to;
};

/// @brief Current stock position with low-stock and expiry flags.
class InventoryReport final : public ReportGenerator {
public:
    explicit InventoryReport(const persistence::IngredientRepository& ingredients);
    QString title() const override;
    QStringList header() const override;             ///< Ingredient | Unit | Stock | Threshold | Low? | Expiry | Unit Cost
    std::vector<QStringList> rows() const override;
private:
    const persistence::IngredientRepository& m_ingredients;
};

/// @brief Every order raised in a date range, with its status and value.
class OrdersReport final : public ReportGenerator {
public:
    OrdersReport(const persistence::OrderRepository& orders, QDate from, QDate to);
    QString title() const override;
    QStringList header() const override;             ///< Order # | Type | Status | Items | Subtotal | Created
    std::vector<QStringList> rows() const override;
private:
    const persistence::OrderRepository& m_orders;
    QDate m_from, m_to;
};

/// @brief The loyalty database as a flat table.
class CustomersReport final : public ReportGenerator {
public:
    explicit CustomersReport(const persistence::CustomerRepository& customers);
    QString title() const override;
    QStringList header() const override;             ///< Name | Phone | Email | Visits | Points
    std::vector<QStringList> rows() const override;
private:
    const persistence::CustomerRepository& m_customers;
};

/// @brief Staff roster with computed pay.
class EmployeesReport final : public ReportGenerator {
public:
    explicit EmployeesReport(const persistence::EmployeeRepository& employees);
    QString title() const override;
    QStringList header() const override;             ///< Name | Position | Shift | Salary | Monthly Pay | Rating
    /// @details Monthly Pay comes from `EmployeeRepository::makeTyped()` and the virtual
    ///          `monthlyPay()` — the report never re-implements any role's pay rule.
    std::vector<QStringList> rows() const override;
private:
    const persistence::EmployeeRepository& m_employees;
};

} // namespace aluchop::services

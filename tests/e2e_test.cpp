/**
 * @file e2e_test.cpp
 * @brief Headless end-to-end assertion of AluChop's business logic.
 *
 * This driver builds a REAL services::AppContext against a THROWAWAY data directory
 * (a fresh folder under the system temp dir), so the migrator seeds a brand-new
 * SQLite database, a brand-new binary audit trail and a brand-new log file. The
 * user's real database in ~/Library/Application Support/AluChop is never opened,
 * read or written by this program.
 *
 * Every check is a hard assertion with a PASS/FAIL line. A failing assertion that
 * reveals a genuine defect is a successful run of this file; nothing here is
 * softened to make the suite go green.
 *
 * The suite runs in ONE pass against the schema EXACTLY AS THE APPLICATION SHIPS IT.
 * No constraint is relaxed, no table is rewritten, nothing is stubbed.
 *
 * Exit code: 0 when every assertion passed, 1 otherwise.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <chrono>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <QApplication>
#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTimeZone>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Money.hpp"
#include "aluchop/core/Result.hpp"
#include "aluchop/gui/PdfExporter.hpp"
#include "aluchop/models/Admin.hpp"
#include "aluchop/models/Bill.hpp"
#include "aluchop/models/Chef.hpp"
#include "aluchop/models/Customer.hpp"
#include "aluchop/models/Employee.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/Ingredient.hpp"
#include "aluchop/models/Manager.hpp"
#include "aluchop/models/MenuItem.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/models/OrderItem.hpp"
#include "aluchop/models/RecipeLine.hpp"
#include "aluchop/models/Reservation.hpp"
#include "aluchop/models/Table.hpp"
#include "aluchop/models/Waiter.hpp"
#include "aluchop/persistence/AuditTrail.hpp"
#include "aluchop/persistence/BinaryRecordFile.hpp"
#include "aluchop/persistence/CsvWriter.hpp"
#include "aluchop/persistence/Database.hpp"
#include "aluchop/services/AppContext.hpp"
#include "aluchop/services/CustomerService.hpp"
#include "aluchop/services/EmployeeService.hpp"
#include "aluchop/services/KitchenQueue.hpp"
#include "aluchop/services/OrderService.hpp"
#include "aluchop/services/ReportGenerator.hpp"
#include "aluchop/services/ReportService.hpp"

using aluchop::core::Money;
namespace models = aluchop::models;
namespace services = aluchop::services;
namespace persistence = aluchop::persistence;
namespace gui = aluchop::gui;

// =============================================================================
// Tiny assertion harness
// =============================================================================
namespace {

int g_total = 0;
int g_passed = 0;
int g_failed = 0;
QStringList g_failures;

void out(const QString& s) { std::cout << s.toStdString() << std::endl; }

void section(const QString& title) {
    out(QString());
    const int pad = std::max(0, 62 - static_cast<int>(title.size()));
    out(QStringLiteral("== %1 %2").arg(title, QString(pad, QLatin1Char('='))));
}

/// One hard assertion.
void check(const QString& name, bool ok, const QString& detail = QString()) {
    ++g_total;
    if (ok) {
        ++g_passed;
        out(QStringLiteral("  PASS  %1").arg(name));
    } else {
        ++g_failed;
        const QString line = detail.isEmpty()
                                 ? name
                                 : QStringLiteral("%1  --  %2").arg(name, detail);
        g_failures << line;
        out(QStringLiteral("  FAIL  %1").arg(line));
    }
}

void checkEq(const QString& name, long long got, long long want) {
    check(name, got == want, QStringLiteral("got %1, expected %2").arg(got).arg(want));
}

void checkNear(const QString& name, double got, double want, double tol) {
    check(name, std::fabs(got - want) <= tol,
          QStringLiteral("got %1, expected %2 (tol %3)").arg(got).arg(want).arg(tol));
}

/// Assert that @p fn throws exception type @p E (and nothing else, and not nothing).
template <typename E>
void checkThrows(const QString& name, const std::function<void()>& fn) {
    QString detail;
    bool ok = false;
    try {
        fn();
        detail = QStringLiteral("no exception was thrown");
    } catch (const E& ex) {
        ok = true;
        detail = QString::fromUtf8(ex.what());
    } catch (const std::exception& ex) {
        detail = QStringLiteral("wrong exception type: %1").arg(QString::fromUtf8(ex.what()));
    } catch (...) {
        detail = QStringLiteral("wrong (non-std) exception type");
    }
    check(name, ok, detail);
}

// -----------------------------------------------------------------------------
// helpers
// -----------------------------------------------------------------------------

/// Parses "Rs 1,250.00" / "-Rs 1,250.00" back into paisa. Returns false on a bad token.
bool parseNpr(const QString& token, long long& paisaOut) {
    QString t = token.trimmed();
    bool negative = false;
    if (t.startsWith(QLatin1Char('-'))) {
        negative = true;
        t = t.mid(1);
    }
    if (!t.startsWith(QStringLiteral("Rs "))) return false;
    t = t.mid(3).remove(QLatin1Char(','));
    const QStringList parts = t.split(QLatin1Char('.'));
    if (parts.size() != 2 || parts[1].size() != 2) return false;
    bool ok1 = false, ok2 = false;
    const long long rupees = parts[0].toLongLong(&ok1);
    const long long paisa = parts[1].toLongLong(&ok2);
    if (!ok1 || !ok2) return false;
    paisaOut = (negative ? -1 : 1) * (rupees * 100 + paisa);
    return true;
}

/// Finds the receipt line whose text starts with @p label and returns the money at its right edge.
bool receiptFigure(const QString& receipt, const QString& label, long long& paisaOut) {
    static const QRegularExpression money(QStringLiteral("(-?Rs [0-9,]+\\.[0-9]{2})\\s*$"));
    const QStringList lines = receipt.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (!line.startsWith(label)) continue;
        const QRegularExpressionMatch m = money.match(line);
        if (!m.hasMatch()) continue;
        return parseNpr(m.captured(1), paisaOut);
    }
    return false;
}

/// Finds a figure on a line of the LOCALE-INDEPENDENT `std::ostream` form of a bill, where
/// `core::operator<<(std::ostream&, const Money&)` renders money as `NPR <rupees>.<paisa>`
/// with no grouping separators at all.
bool streamedFigure(const std::string& text, const QString& label, long long& paisaOut) {
    static const QRegularExpression money(QStringLiteral("(-?NPR [0-9]+\\.[0-9]{2})\\s*$"));
    const QStringList lines = QString::fromStdString(text).split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (!line.startsWith(label)) continue;
        const QRegularExpressionMatch m = money.match(line);
        if (!m.hasMatch()) continue;
        QString t = m.captured(1);
        const bool negative = t.startsWith(QLatin1Char('-'));
        if (negative) t = t.mid(1);
        t = t.mid(4);                                   // drop the "NPR " prefix
        const QStringList parts = t.split(QLatin1Char('.'));
        if (parts.size() != 2 || parts[1].size() != 2) return false;
        bool ok1 = false, ok2 = false;
        const long long rupees = parts[0].toLongLong(&ok1);
        const long long paisa = parts[1].toLongLong(&ok2);
        if (!ok1 || !ok2) return false;
        paisaOut = (negative ? -1 : 1) * (rupees * 100 + paisa);
        return true;
    }
    return false;
}

/// Streams a bill through `models::operator<<` — the archival plain-text path — and returns it.
std::string streamed(const models::Bill& bill) {
    std::ostringstream os;
    os << bill;                    // found by ADL: the friend declared inside models::Bill
    return os.str();
}

/// Collapses every run of whitespace to a single space, so a DDL assertion can be written
/// against the column declaration rather than against the migrator's indentation.
QString squeezed(const QString& s) {
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    QString t = s;
    return t.replace(ws, QStringLiteral(" "));
}

long long scalarLongLong(const QString& sql, const QVariantList& binds) {
    QSqlQuery q = persistence::Database::instance().prepared(sql, binds);
    return q.next() ? q.value(0).toLongLong() : -1;
}

QString scalarString(const QString& sql, const QVariantList& binds) {
    QSqlQuery q = persistence::Database::instance().prepared(sql, binds);
    return q.next() ? q.value(0).toString() : QString();
}

/// Aggregate expected ingredient draw for one order, computed independently from the recipes.
std::map<int, double> plannedDraw(services::AppContext& ctx, const models::Order& order) {
    std::map<int, double> planned;
    for (const models::OrderItem& line : order.items()) {
        if (line.menuItemId() == 0) continue;
        for (const models::RecipeLine& r : ctx.menu().recipeFor(line.menuItemId())) {
            if (r.qtyPerServing <= 0.0) continue;
            planned[r.ingredientId] += r.qtyPerServing * static_cast<double>(line.qty());
        }
    }
    return planned;
}

/// Tallies dish -> qty for an order, loading it into a NAMED optional first.
/// (Iterating `ctx.orders().order(id)->items()` directly dangles: the optional is a temporary
/// destroyed at the end of the full-expression, before the loop body ever runs.)
void tallyOrder(services::AppContext& ctx, int orderId, std::map<QString, int>& into) {
    const std::optional<models::Order> o = ctx.orders().order(orderId);
    if (!o) return;
    for (const models::OrderItem& li : o->items()) into[li.name()] += li.qty();
}

std::map<int, double> stockSnapshot(services::AppContext& ctx) {
    std::map<int, double> snap;
    for (const models::Ingredient& i : ctx.inventory().all()) snap[i.id()] = i.stockQty();
    return snap;
}

/// Drives an Open order all the way to Served through the public service ladder.
bool serveOrder(services::AppContext& ctx, int orderId, QString& why) {
    auto fired = ctx.orders().submitToKitchen(orderId);
    if (fired.isErr()) { why = fired.error(); return false; }
    for (int i = 0; i < 3; ++i) {
        auto step = ctx.orders().advanceStatus(orderId);
        if (step.isErr()) { why = step.error(); return false; }
    }
    const auto o = ctx.orders().order(orderId);
    if (!o || o->status() != models::OrderStatus::Served) {
        why = QStringLiteral("order did not reach Served");
        return false;
    }
    return true;
}

// =============================================================================
// Shared fixture state
// =============================================================================

struct Fixture {
    QString dataDir;
    std::vector<models::MenuItem> dishes;      ///< available dishes that have a recipe
    int tableA = 0;
    int tableB = 0;
    long long expectedRevenuePaisa = 0;        ///< every paisa this test settles
    std::map<QString, int> soldQty;            ///< dish name -> qty on SETTLED orders
    int paidOrderId = 0;                       ///< the order used for the round-trip check
    int outstandingOrders = 0;                 ///< orders parked in Pending/Preparing/Ready
};

// =============================================================================
// 1. Authentication & authorisation
// =============================================================================

void testAuth(services::AppContext& ctx) {
    section(QStringLiteral("AUTH"));

    auto good = ctx.auth().login(QStringLiteral("admin"), QStringLiteral("admin123"));
    check(QStringLiteral("admin logs in with the seeded credentials"), good.isOk(),
          good.isOk() ? QString() : good.error());
    if (good.isOk())
        check(QStringLiteral("the seeded admin holds the Admin role"),
              good.value().role() == models::UserRole::Admin);

    auto bad = ctx.auth().login(QStringLiteral("admin"), QStringLiteral("not-my-password"));
    check(QStringLiteral("a wrong password is REJECTED"), bad.isErr(),
          bad.isOk() ? QStringLiteral("login succeeded with a bad password!") : QString());

    // A failed login must not silently keep the previous session's rights either way round;
    // re-establish the admin session explicitly before the privileged calls below.
    ctx.auth().login(QStringLiteral("admin"), QStringLiteral("admin123"));

    const QString storedHash =
        scalarString(QStringLiteral("SELECT pass_hash FROM users WHERE username = ?"),
                     {QStringLiteral("admin")});
    const QString storedSalt =
        scalarString(QStringLiteral("SELECT salt FROM users WHERE username = ?"),
                     {QStringLiteral("admin")});

    check(QStringLiteral("the stored credential is not the plaintext password"),
          storedHash != QStringLiteral("admin123") && !storedHash.isEmpty());
    check(QStringLiteral("the stored credential is a 64-hex SHA-256 digest"),
          storedHash.size() == 64,
          QStringLiteral("hash length %1").arg(storedHash.size()));
    check(QStringLiteral("a per-user salt is stored alongside it"), storedSalt.size() >= 16,
          QStringLiteral("salt '%1'").arg(storedSalt));
    check(QStringLiteral("stored hash == SHA-256(salt + password)"),
          services::AuthService::hashPassword(QStringLiteral("admin123"), storedSalt) == storedHash);
    check(QStringLiteral("the same password with a different salt yields a different hash"),
          services::AuthService::hashPassword(QStringLiteral("admin123"), storedSalt)
              != services::AuthService::hashPassword(QStringLiteral("admin123"),
                                                     storedSalt + QStringLiteral("ff")));

    // Two brand-new accounts sharing one password must not share a digest.
    auto u1 = ctx.auth().createUser(QStringLiteral("twin_one"), QStringLiteral("samepass123"),
                                    models::UserRole::Waiter, 0,
                                    QStringLiteral("Q?"), QStringLiteral("A"));
    auto u2 = ctx.auth().createUser(QStringLiteral("twin_two"), QStringLiteral("samepass123"),
                                    models::UserRole::Manager, 0,
                                    QStringLiteral("Q?"), QStringLiteral("A"));
    check(QStringLiteral("admin can create login accounts"), u1.isOk() && u2.isOk(),
          u1.isErr() ? u1.error() : (u2.isErr() ? u2.error() : QString()));

    const QString h1 = scalarString(QStringLiteral("SELECT pass_hash FROM users WHERE username = ?"),
                                    {QStringLiteral("twin_one")});
    const QString h2 = scalarString(QStringLiteral("SELECT pass_hash FROM users WHERE username = ?"),
                                    {QStringLiteral("twin_two")});
    const QString s1 = scalarString(QStringLiteral("SELECT salt FROM users WHERE username = ?"),
                                    {QStringLiteral("twin_one")});
    const QString s2 = scalarString(QStringLiteral("SELECT salt FROM users WHERE username = ?"),
                                    {QStringLiteral("twin_two")});
    check(QStringLiteral("two users with the SAME password get DIFFERENT salts"),
          !s1.isEmpty() && s1 != s2);
    check(QStringLiteral("two users with the SAME password get DIFFERENT hashes (salt is real)"),
          !h1.isEmpty() && h1 != h2);
    check(QStringLiteral("neither stored hash is the plaintext"),
          h1 != QStringLiteral("samepass123") && h2 != QStringLiteral("samepass123"));

    // --- role gate ---------------------------------------------------------
    check(QStringLiteral("admin session outranks Admin"),
          ctx.auth().hasRole(models::UserRole::Admin));

    auto asWaiter = ctx.auth().login(QStringLiteral("twin_one"), QStringLiteral("samepass123"));
    check(QStringLiteral("the newly created waiter can log in"), asWaiter.isOk(),
          asWaiter.isErr() ? asWaiter.error() : QString());
    check(QStringLiteral("a waiter does NOT satisfy hasRole(Admin)"),
          !ctx.auth().hasRole(models::UserRole::Admin));
    check(QStringLiteral("a waiter DOES satisfy hasRole(Waiter)"),
          ctx.auth().hasRole(models::UserRole::Waiter));

    auto denied = ctx.auth().createUser(QStringLiteral("sneaky"), QStringLiteral("password1"),
                                        models::UserRole::Admin, 0, QString(), QString());
    check(QStringLiteral("role gate BITES: a waiter cannot create a login account"), denied.isErr(),
          denied.isOk() ? QStringLiteral("a waiter created an admin account!") : denied.error());
    checkEq(QStringLiteral("...and no such row reached the database"),
            scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM users WHERE username = ?"),
                           {QStringLiteral("sneaky")}),
            0);

    // --- remember-me ------------------------------------------------------
    // `rememberMe = false` makes AuthService bind a DEFAULT-CONSTRUCTED QString into
    // `users.remember_token TEXT NOT NULL DEFAULT ''`. A default-constructed QString is *null*,
    // not empty, and QSQLITE binds a null QString as SQL NULL — which a column DEFAULT never
    // rescues on an UPDATE. That is the exact statement that used to abort every single login
    // with "NOT NULL constraint failed: users.remember_token".
    auto plain = ctx.auth().login(QStringLiteral("admin"), QStringLiteral("admin123"), false);
    check(QStringLiteral("LOGIN WITH rememberMe = FALSE SUCCEEDS"), plain.isOk(),
          plain.isErr() ? plain.error() : QString());
    checkEq(QStringLiteral("...and users.remember_token is stored as '' — never as SQL NULL"),
            scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM users WHERE username = ? "
                                          "AND remember_token IS NOT NULL AND remember_token = ''"),
                           {QStringLiteral("admin")}),
            1);

    auto remembered = ctx.auth().login(QStringLiteral("admin"), QStringLiteral("admin123"), true);
    check(QStringLiteral("login with rememberMe = TRUE succeeds"), remembered.isOk(),
          remembered.isErr() ? remembered.error() : QString());
    const QString token =
        scalarString(QStringLiteral("SELECT remember_token FROM users WHERE username = ?"),
                     {QStringLiteral("admin")});
    check(QStringLiteral("...and a real remember-me token is persisted"), token.size() >= 16,
          QStringLiteral("token '%1'").arg(token));
    {
        const std::optional<models::User> silent = ctx.auth().tryRememberedLogin();
        check(QStringLiteral("the stored token signs the same user back in silently"),
              silent.has_value() && silent->username() == QStringLiteral("admin"),
              silent ? silent->username() : QStringLiteral("(nobody)"));
    }

    ctx.auth().logout();
    check(QStringLiteral("after logout nobody is signed in"), !ctx.auth().isLoggedIn());
    check(QStringLiteral("a signed-out session satisfies no role at all"),
          !ctx.auth().hasRole(models::UserRole::Waiter));
    checkEq(QStringLiteral("LOGOUT CLEARS the remember-me token to '' — again never to NULL"),
            scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM users WHERE username = ? "
                                          "AND remember_token IS NOT NULL AND remember_token = ''"),
                           {QStringLiteral("admin")}),
            1);
    check(QStringLiteral("...and the cleared token signs nobody in any more"),
          !ctx.auth().tryRememberedLogin().has_value());

    // --- forgot-password: recovery question -------------------------------
    auto question = ctx.auth().securityQuestionFor(QStringLiteral("twin_one"));
    check(QStringLiteral("the recovery question can be fetched for a real account"),
          question.isOk() && question.value() == QStringLiteral("Q?"),
          question.isErr() ? question.error() : question.value());
    check(QStringLiteral("...and is refused for an account that does not exist"),
          ctx.auth().securityQuestionFor(QStringLiteral("nobody_at_all")).isErr());

    auto wrongAnswer = ctx.auth().resetPasswordWithAnswer(
        QStringLiteral("twin_one"), QStringLiteral("not-the-answer"),
        QStringLiteral("brandnew123"));
    check(QStringLiteral("a WRONG recovery answer does NOT reset the password"),
          wrongAnswer.isErr(),
          wrongAnswer.isOk() ? QStringLiteral("the password was reset on a wrong answer!")
                             : wrongAnswer.error());
    check(QStringLiteral("...and the original password still works after the refusal"),
          ctx.auth().login(QStringLiteral("twin_one"), QStringLiteral("samepass123")).isOk());

    check(QStringLiteral("a reset to a password under the %1-character minimum is refused")
              .arg(services::AuthService::kMinPasswordLength),
          ctx.auth()
              .resetPasswordWithAnswer(QStringLiteral("twin_one"), QStringLiteral("A"),
                                       QStringLiteral("abc"))
              .isErr());

    auto reset = ctx.auth().resetPasswordWithAnswer(
        QStringLiteral("twin_one"), QStringLiteral("A"), QStringLiteral("brandnew123"));
    check(QStringLiteral("the RIGHT recovery answer RESETS the password"), reset.isOk(),
          reset.isErr() ? reset.error() : QString());
    check(QStringLiteral("...the new password logs in"),
          ctx.auth().login(QStringLiteral("twin_one"), QStringLiteral("brandnew123")).isOk());
    check(QStringLiteral("...the OLD password no longer does"),
          ctx.auth().login(QStringLiteral("twin_one"), QStringLiteral("samepass123")).isErr());
    checkEq(QStringLiteral("...and the reset cleared that account's remember-me token to ''"),
            scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM users WHERE username = ? "
                                          "AND remember_token IS NOT NULL AND remember_token = ''"),
                           {QStringLiteral("twin_one")}),
            1);
    check(QStringLiteral("...and the stored hash is still SHA-256(salt + the NEW password)"),
          services::AuthService::hashPassword(
              QStringLiteral("brandnew123"),
              scalarString(QStringLiteral("SELECT salt FROM users WHERE username = ?"),
                           {QStringLiteral("twin_one")}))
              == scalarString(QStringLiteral("SELECT pass_hash FROM users WHERE username = ?"),
                              {QStringLiteral("twin_one")}));

    // --- change password (signed in) ---------------------------------------
    check(QStringLiteral("twin_two signs in with its original password"),
          ctx.auth().login(QStringLiteral("twin_two"), QStringLiteral("samepass123")).isOk());
    check(QStringLiteral("changePassword refuses a WRONG current password"),
          ctx.auth().changePassword(QStringLiteral("wrong-old"), QStringLiteral("freshpass1"))
              .isErr());
    check(QStringLiteral("changePassword refuses a new password identical to the old one"),
          ctx.auth().changePassword(QStringLiteral("samepass123"), QStringLiteral("samepass123"))
              .isErr());
    auto changed =
        ctx.auth().changePassword(QStringLiteral("samepass123"), QStringLiteral("freshpass1"));
    check(QStringLiteral("changePassword succeeds with the right current password"),
          changed.isOk(), changed.isErr() ? changed.error() : QString());
    ctx.auth().logout();
    check(QStringLiteral("...the changed password logs in"),
          ctx.auth().login(QStringLiteral("twin_two"), QStringLiteral("freshpass1")).isOk());
    check(QStringLiteral("...the superseded password does not"),
          ctx.auth().login(QStringLiteral("twin_two"), QStringLiteral("samepass123")).isErr());

    auto back = ctx.auth().login(QStringLiteral("admin"), QStringLiteral("admin123"));
    check(QStringLiteral("admin session restored for the remaining tests"), back.isOk());
}

// =============================================================================
// 2. Order lifecycle
// =============================================================================

void testOrderLifecycle(services::AppContext& ctx, Fixture& fx) {
    section(QStringLiteral("ORDER LIFECYCLE"));

    auto created = ctx.orders().createOrder(models::OrderType::DineIn, fx.tableA, 0, 0);
    check(QStringLiteral("a Dine-In order can be created on a real table"), created.isOk(),
          created.isErr() ? created.error() : QString());
    if (created.isErr()) return;
    const int id = created.value().id();
    check(QStringLiteral("the new order is persisted with an id and an ORD- number"),
          id > 0 && created.value().orderNumber().startsWith(QStringLiteral("ORD-")),
          created.value().orderNumber());

    const models::MenuItem& d0 = fx.dishes[0];
    const models::MenuItem& d1 = fx.dishes[1];
    check(QStringLiteral("add 2 x dish A"), ctx.orders().addItem(id, d0.id(), 2).isOk());
    check(QStringLiteral("add 1 x dish B"), ctx.orders().addItem(id, d1.id(), 1).isOk());
    check(QStringLiteral("add 3 x dish A again"), ctx.orders().addItem(id, d0.id(), 3).isOk());

    auto loaded = ctx.orders().order(id);
    check(QStringLiteral("the order reloads from the database"), loaded.has_value());
    if (!loaded) return;

    checkEq(QStringLiteral("repeated dishes COMBINE into one line (2 lines, not 3)"),
            static_cast<long long>(loaded->itemCount()), 2);
    long long qtyA = 0;
    for (const models::OrderItem& li : loaded->items())
        if (li.menuItemId() == d0.id()) qtyA = li.qty();
    checkEq(QStringLiteral("quantities combine correctly (2 + 3 == 5)"), qtyA, 5);

    // --- the illegal jump --------------------------------------------------
    {
        models::Order fresh;   // status Open by construction
        checkThrows<aluchop::core::ValidationException>(
            QStringLiteral("Order::setStatus rejects the illegal jump Open -> Served"),
            [&fresh] { fresh.setStatus(models::OrderStatus::Served); });
        check(QStringLiteral("...and the order is still Open after the refusal"),
              fresh.status() == models::OrderStatus::Open);
    }
    auto jump = ctx.orders().advanceStatus(id);   // Open cannot be advanced by the service either
    check(QStringLiteral("advanceStatus refuses to advance an Open order"), jump.isErr(),
          jump.isOk() ? QStringLiteral("an Open order advanced itself") : jump.error());

    // --- the legal ladder --------------------------------------------------
    check(QStringLiteral("Open -> Pending (submit to kitchen)"),
          ctx.orders().submitToKitchen(id).isOk());
    check(QStringLiteral("...status is Pending"),
          ctx.orders().order(id)->status() == models::OrderStatus::Pending);
    check(QStringLiteral("...the ticket is on the kitchen queue"),
          !ctx.orders().kitchenQueue().empty());

    check(QStringLiteral("Pending -> Preparing"), ctx.orders().advanceStatus(id).isOk());
    check(QStringLiteral("...status is Preparing"),
          ctx.orders().order(id)->status() == models::OrderStatus::Preparing);

    check(QStringLiteral("Preparing -> Ready"), ctx.orders().advanceStatus(id).isOk());
    check(QStringLiteral("...status is Ready"),
          ctx.orders().order(id)->status() == models::OrderStatus::Ready);

    // --- cancelling a Ready order -----------------------------------------
    auto lateVoid = ctx.orders().cancelOrder(id);
    check(QStringLiteral("cancelling a READY order is REJECTED (documented rule)"), lateVoid.isErr(),
          lateVoid.isOk() ? QStringLiteral("a plated order was voided") : lateVoid.error());
    check(QStringLiteral("...and the order is still Ready afterwards"),
          ctx.orders().order(id)->status() == models::OrderStatus::Ready);

    check(QStringLiteral("Ready -> Served"), ctx.orders().advanceStatus(id).isOk());
    check(QStringLiteral("...status is Served"),
          ctx.orders().order(id)->status() == models::OrderStatus::Served);

    // --- Served -> Paid closes the ladder ----------------------------------
    auto bill = ctx.billing().prepareBill(id);
    check(QStringLiteral("a bill can be raised on a Served order"), bill.isOk(),
          bill.isErr() ? bill.error() : QString());
    if (bill.isOk()) {
        const Money total = bill.value().total();
        auto paid = ctx.billing().settle(id, bill.value(), models::PaymentMethod::Cash,
                                         total, 1);
        check(QStringLiteral("Served -> Paid (settlement)"), paid.isOk(),
              paid.isErr() ? paid.error() : QString());
        check(QStringLiteral("...status is Paid"),
              ctx.orders().order(id)->status() == models::OrderStatus::Paid);
        if (paid.isOk()) {
            fx.expectedRevenuePaisa += total.paisa();
            tallyOrder(ctx, id, fx.soldQty);
            fx.paidOrderId = id;
        }
    }

    // A Paid order is the end of the road.
    auto beyond = ctx.orders().advanceStatus(id);
    check(QStringLiteral("a Paid order cannot be advanced any further"), beyond.isErr());
}

// =============================================================================
// 3. The money rules
// =============================================================================

void testMoney(services::AppContext& ctx, Fixture& fx) {
    section(QStringLiteral("MONEY RULES"));

    // ---------------------------------------------------------------- order M1
    auto m1 = ctx.orders().createOrder(models::OrderType::DineIn, fx.tableB, 0, 0);
    if (m1.isErr()) { check(QStringLiteral("money order created"), false, m1.error()); return; }
    const int id = m1.value().id();

    struct Want { int menuId; int qty; };
    const std::vector<Want> wanted = {{fx.dishes[0].id(), 3},
                                      {fx.dishes[1].id(), 1},
                                      {fx.dishes[2].id(), 2}};
    for (const Want& w : wanted) ctx.orders().addItem(id, w.menuId, w.qty);

    auto order = ctx.orders().order(id);
    check(QStringLiteral("money order reloads"), order.has_value());
    if (!order) return;

    // (a) subtotal == sum of line totals, in integer paisa
    long long lineSum = 0;
    for (const models::OrderItem& li : order->items()) {
        checkEq(QStringLiteral("line total == unit price x qty for '%1'").arg(li.name()),
                li.lineTotal().paisa(), li.unitPrice().paisa() * li.qty());
        lineSum += li.lineTotal().paisa();
    }
    checkEq(QStringLiteral("order subtotal == sum of line totals (integer paisa)"),
            order->subtotal().paisa(), lineSum);

    // (b) the grand total of un-discounted items == sum of the REAL seeded menu prices
    long long menuSum = 0;
    for (const Want& w : wanted) {
        const long long price =
            scalarLongLong(QStringLiteral("SELECT price_paisa FROM menu_items WHERE id = ?"),
                           {w.menuId});
        menuSum += price * w.qty;
    }

    QString why;
    if (!serveOrder(ctx, id, why)) {
        check(QStringLiteral("money order reaches Served"), false, why);
        return;
    }

    auto billR = ctx.billing().prepareBill(id);           // no promo, no service charge
    check(QStringLiteral("plain bill prepared"), billR.isOk(),
          billR.isErr() ? billR.error() : QString());
    if (billR.isErr()) return;
    models::Bill bill = billR.value();

    checkEq(QStringLiteral("TAX IS NEVER ADDED: grand total == sum of seeded menu prices exactly"),
            bill.total().paisa(), menuSum);
    checkEq(QStringLiteral("...and the bill subtotal equals it too"),
            bill.subtotal().paisa(), menuSum);
    check(QStringLiteral("...the total is NOT the subtotal grossed up by 13%"),
          bill.total().paisa() != (menuSum * 113) / 100,
          QStringLiteral("total %1, +13%% would be %2").arg(bill.total().paisa())
              .arg((menuSum * 113) / 100));

    // (c) the VAT line is a REVERSE computation of the inclusive price
    const QString receipt = ctx.billing().receiptText(bill);
    long long rTotal = -1, rTaxable = -1, rVat = -1;
    const bool gotTotal = receiptFigure(receipt, QStringLiteral("TOTAL"), rTotal);
    const bool gotTaxable = receiptFigure(receipt, QStringLiteral("Taxable amount"), rTaxable);
    const bool gotVat = receiptFigure(receipt, QStringLiteral("VAT "), rVat);
    check(QStringLiteral("the receipt prints TOTAL, Taxable amount and a VAT line"),
          gotTotal && gotTaxable && gotVat,
          QStringLiteral("total=%1 taxable=%2 vat=%3").arg(gotTotal).arg(gotTaxable).arg(gotVat));
    if (gotTotal && gotTaxable && gotVat) {
        checkEq(QStringLiteral("the printed TOTAL is the bill total"), rTotal, bill.total().paisa());
        checkEq(QStringLiteral("taxable + VAT == gross, EXACTLY (no rounding drift)"),
                rTaxable + rVat, rTotal);
        check(QStringLiteral("VAT is reverse-computed at 13% of the NET (not 13% of the gross)"),
              std::llabs(rTaxable * 13 - rVat * 100) <= 113,
              QStringLiteral("net %1, vat %2 (13%% of gross would be %3)")
                  .arg(rTaxable).arg(rVat).arg((rTotal * 13) / 100));
        check(QStringLiteral("the receipt discloses tax inclusivity"),
              receipt.contains(QStringLiteral("All prices are inclusive of tax.")));
    }

    // (d) tender rules and change
    const Money total = bill.total();
    auto short_ = ctx.billing().settle(id, bill, models::PaymentMethod::Cash,
                                       total - Money::fromRupees(1), 1);
    check(QStringLiteral("a tender BELOW the total is REJECTED"), short_.isErr(),
          short_.isOk() ? QStringLiteral("the till accepted a short payment") : short_.error());
    check(QStringLiteral("...and the bill is still unsettled"), !bill.isSettled());

    const Money tendered = total + Money::fromRupees(500);
    auto paid = ctx.billing().settle(id, bill, models::PaymentMethod::Cash, tendered, 1);
    check(QStringLiteral("an adequate cash tender settles the bill"), paid.isOk(),
          paid.isErr() ? paid.error() : QString());
    if (paid.isOk()) {
        checkEq(QStringLiteral("change is computed EXACTLY (tendered - total)"),
                paid.value().change().paisa(), tendered.paisa() - total.paisa());
        checkEq(QStringLiteral("the payment row records the exact total"),
                paid.value().total().paisa(), total.paisa());
        check(QStringLiteral("the bill is now marked settled"), bill.isSettled());
        fx.expectedRevenuePaisa += total.paisa();
        for (const models::OrderItem& li : order->items()) fx.soldQty[li.name()] += li.qty();
    }

    auto twice = ctx.billing().settle(id, bill, models::PaymentMethod::Cash, tendered, 1);
    check(QStringLiteral("a settled bill cannot be settled a second time"), twice.isErr());

    // ---------------------------------------------------------------- operator<<
    // The archival plain-text receipt — the same figures, streamed through
    // models::operator<<(std::ostream&, const Bill&) and core::operator<<(ostream&, const Money&).
    // This is the path BillingService::settle() appends to the raw <fstream> journal.
    {
        const std::string text = streamed(bill);
        const QString qtext = QString::fromStdString(text);

        check(QStringLiteral("operator<< names the order it belongs to"),
              qtext.contains(bill.orderNumber()) && qtext.startsWith(QStringLiteral("BILL ")),
              qtext.left(60));

        long long sSub = -1, sTotal = -1, sVat = -1, sTendered = -1, sChange = -1;
        const bool gotSub = streamedFigure(text, QStringLiteral("SUBTOTAL"), sSub);
        const bool gotTot = streamedFigure(text, QStringLiteral("TOTAL"), sTotal);
        const bool gotVat = streamedFigure(text, QStringLiteral("VAT "), sVat);
        const bool gotTen = streamedFigure(text, QStringLiteral("TENDERED"), sTendered);
        const bool gotChg = streamedFigure(text, QStringLiteral("CHANGE"), sChange);
        check(QStringLiteral("operator<< prints SUBTOTAL, TOTAL, VAT, TENDERED and CHANGE"),
              gotSub && gotTot && gotVat && gotTen && gotChg,
              QStringLiteral("sub=%1 total=%2 vat=%3 tendered=%4 change=%5")
                  .arg(gotSub).arg(gotTot).arg(gotVat).arg(gotTen).arg(gotChg));

        if (gotSub && gotTot && gotVat && gotTen && gotChg) {
            checkEq(QStringLiteral("THE MONEY IS UNCHANGED: streamed SUBTOTAL == bill subtotal"),
                    sSub, bill.subtotal().paisa());
            checkEq(QStringLiteral("streamed TOTAL == bill total == the seeded menu prices"),
                    sTotal, menuSum);
            checkEq(QStringLiteral("streamed TOTAL == the TOTAL on the guest-facing receipt"),
                    sTotal, rTotal);
            checkEq(QStringLiteral("streamed TENDERED == what was handed over"),
                    sTendered, tendered.paisa());
            checkEq(QStringLiteral("streamed CHANGE == tendered - total, to the paisa"),
                    sChange, tendered.paisa() - total.paisa());
            checkEq(QStringLiteral("the streamed VAT line is the SAME disclosure figure as the "
                                   "receipt's (no second, divergent computation)"),
                    sVat, rVat);
            check(QStringLiteral("...and it is a DISCLOSURE, not an addition: VAT < TOTAL and "
                                 "TOTAL is still exactly the subtotal"),
                  sVat < sTotal && sTotal == sSub,
                  QStringLiteral("vat %1, total %2, subtotal %3").arg(sVat).arg(sTotal).arg(sSub));
        }

        check(QStringLiteral("operator<< records the tender type and the settled state"),
              qtext.contains(QStringLiteral("PAID BY")) && !qtext.contains(QStringLiteral("UNSETTLED")),
              qtext.left(400));
        check(QStringLiteral("operator<< discloses tax inclusivity too"),
              qtext.contains(QStringLiteral("All prices are inclusive of tax.")));
        static const QRegularExpression grouped(QStringLiteral("NPR [0-9]*,"));
        check(QStringLiteral("the streamed form is LOCALE-INDEPENDENT: 'NPR', and not one "
                             "grouping separator inside a money token"),
              qtext.contains(QStringLiteral("NPR ")) && !qtext.contains(QStringLiteral("Rs "))
                  && !grouped.match(qtext).hasMatch(),
              qtext.left(200));

        // Every line the guest was charged for is on the archival copy, with its own money.
        bool everyLine = true;
        QString missing;
        for (const models::OrderItem& li : bill.items()) {
            const QString want = QStringLiteral("%1 x %2").arg(li.qty()).arg(li.name());
            long long linePaisa = -1;
            const bool listed = qtext.contains(want)
                                && streamedFigure(text, want, linePaisa)
                                && linePaisa == li.unitPrice().paisa() * li.qty();
            if (!listed && missing.isEmpty()) missing = want;
            everyLine = everyLine && listed;
        }
        check(QStringLiteral("every line is streamed as 'qty x dish' with unit price x qty"),
              everyLine, missing);

        // An UNSETTLED bill says so, and prints no tender.
        models::Order draft;
        draft.addItem(101, QStringLiteral("Momo"), Money::fromRupees(400), 2);
        const models::Bill unsettled(draft);
        const QString us = QString::fromStdString(streamed(unsettled));
        check(QStringLiteral("an UNSETTLED bill streams as UNSETTLED, with no tender line"),
              us.contains(QStringLiteral("UNSETTLED")) && !us.contains(QStringLiteral("PAID BY")),
              us.left(400));
        long long draftTotal = -1;
        check(QStringLiteral("...and an unsaved bill still streams its money correctly"),
              streamedFigure(us.toStdString(), QStringLiteral("TOTAL"), draftTotal)
                  && draftTotal == 80000,
              QStringLiteral("streamed total %1, expected 80000").arg(draftTotal));
    }

    // ------------------------------------------------ order M2: promo vs staff
    // The customer's phone matches an ACTIVE seeded employee, which is what makes
    // EmployeeService::staffCustomerFor() fuse them into a StaffCustomer.
    const QString staffPhone =
        scalarString(QStringLiteral("SELECT phone FROM employees WHERE is_active = 1 "
                                    "AND position = 'WAITER' ORDER BY id LIMIT 1"), {});
    auto staffGuest = ctx.customers().create(QStringLiteral("Prakash Rai (staff)"), staffPhone,
                                             QStringLiteral("staff.guest@aluchop.com.np"));
    check(QStringLiteral("a customer sharing an employee's phone can be registered"),
          staffGuest.isOk(), staffGuest.isErr() ? staffGuest.error() : QString());
    if (staffGuest.isErr()) return;

    auto m2 = ctx.orders().createOrder(models::OrderType::DineIn, fx.tableA,
                                       staffGuest.value(), 0);
    if (m2.isErr()) { check(QStringLiteral("staff order created"), false, m2.error()); return; }
    const int sid = m2.value().id();

    // Build a basket comfortably over the FLAT100 minimum of Rs 1,000 so both discounts qualify.
    long long basket = 0;
    for (std::size_t i = 0; i < fx.dishes.size() && basket < 150000; ++i) {
        ctx.orders().addItem(sid, fx.dishes[i].id(), 2);
        basket += fx.dishes[i].price().paisa() * 2;
    }
    if (!serveOrder(ctx, sid, why)) {
        check(QStringLiteral("staff order reaches Served"), false, why);
        return;
    }

    auto sBillR = ctx.billing().prepareBill(sid, QStringLiteral("FLAT100"), 10);
    check(QStringLiteral("bill with promo + staff eligibility + 10% service charge prepared"),
          sBillR.isOk(), sBillR.isErr() ? sBillR.error() : QString());
    if (sBillR.isErr()) return;
    models::Bill sBill = sBillR.value();

    const Money subtotal = sBill.subtotal();
    const Money staffOff = subtotal.percent(10);
    const Money promoOff = Money::fromRupees(100);      // FLAT100, as seeded
    const Money biggest = staffOff > promoOff ? staffOff : promoOff;

    check(QStringLiteral("the basket clears the FLAT100 minimum of Rs 1,000"),
          subtotal.paisa() >= 100000, subtotal.toString());
    check(QStringLiteral("the staff discount is the larger of the two candidates"),
          staffOff > promoOff, QStringLiteral("staff %1 vs promo %2")
                                   .arg(staffOff.toString(), promoOff.toString()));
    checkEq(QStringLiteral("DISCOUNTS DO NOT STACK: only the LARGEST candidate applies"),
            sBill.discount().paisa(), biggest.paisa());
    check(QStringLiteral("...the two are NOT summed"),
          sBill.discount().paisa() != staffOff.paisa() + promoOff.paisa());
    check(QStringLiteral("...and the losing promo code is not recorded on the bill"),
          sBill.promoCode().isEmpty(), sBill.promoCode());
    check(QStringLiteral("...the discount label names the winner"),
          sBill.discountLabel().startsWith(QStringLiteral("Staff")), sBill.discountLabel());

    checkEq(QStringLiteral("the service charge is 10% of the SUBTOTAL"),
            sBill.serviceCharge().paisa(), subtotal.percent(10).paisa());
    check(QStringLiteral("...i.e. it is NOT levied on the discounted amount"),
          sBill.serviceCharge().paisa() != (subtotal - sBill.discount()).percent(10).paisa(),
          QStringLiteral("svc %1, 10%% of discounted would be %2")
              .arg(sBill.serviceCharge().paisa())
              .arg((subtotal - sBill.discount()).percent(10).paisa()));
    checkEq(QStringLiteral("SERVICE CHARGE IS APPLIED AFTER THE DISCOUNT: "
                           "total == subtotal - discount + service"),
            sBill.total().paisa(),
            subtotal.paisa() - sBill.discount().paisa() + sBill.serviceCharge().paisa());
    check(QStringLiteral("...and still no tax term appears anywhere in the total"),
          sBill.total().paisa()
              == subtotal.paisa() - sBill.discount().paisa() + sBill.serviceCharge().paisa());

    auto sPaid = ctx.billing().settle(sid, sBill, models::PaymentMethod::Card,
                                      sBill.total(), 1);
    check(QStringLiteral("a card payment settles at the exact total"), sPaid.isOk(),
          sPaid.isErr() ? sPaid.error() : QString());
    if (sPaid.isOk()) {
        checkEq(QStringLiteral("a card payment returns zero change"),
                sPaid.value().change().paisa(), 0);
        fx.expectedRevenuePaisa += sBill.total().paisa();
        tallyOrder(ctx, sid, fx.soldQty);
    }

    // ------------------------------------------------ order M3: promo on its own
    auto guest = ctx.customers().create(QStringLiteral("Ordinary Guest"),
                                        QStringLiteral("+977-9800999123"),
                                        QStringLiteral("guest@example.com"));
    if (guest.isErr()) { check(QStringLiteral("plain guest registered"), false, guest.error()); return; }

    auto m3 = ctx.orders().createOrder(models::OrderType::Takeaway, 0, guest.value(), 0);
    if (m3.isErr()) { check(QStringLiteral("promo order created"), false, m3.error()); return; }
    const int pid = m3.value().id();
    ctx.orders().addItem(pid, fx.dishes[0].id(), 2);
    ctx.orders().addItem(pid, fx.dishes[1].id(), 1);
    if (!serveOrder(ctx, pid, why)) {
        check(QStringLiteral("promo order reaches Served"), false, why);
        return;
    }

    auto pBillR = ctx.billing().prepareBill(pid, QStringLiteral("WELCOME10"), 0);
    check(QStringLiteral("bill with a 10% promo prepared"), pBillR.isOk(),
          pBillR.isErr() ? pBillR.error() : QString());
    if (pBillR.isErr()) return;
    models::Bill pBill = pBillR.value();
    checkEq(QStringLiteral("a percentage promo takes exactly 10% off the subtotal"),
            pBill.discount().paisa(), pBill.subtotal().percent(10).paisa());
    check(QStringLiteral("the winning promo code is recorded on the bill"),
          pBill.promoCode() == QStringLiteral("WELCOME10"), pBill.promoCode());
    checkEq(QStringLiteral("total == subtotal - promo discount (no tax, no service charge)"),
            pBill.total().paisa(), pBill.subtotal().paisa() - pBill.discount().paisa());

    auto pPaid = ctx.billing().settle(pid, pBill, models::PaymentMethod::Wallet,
                                      pBill.total(), 1);
    check(QStringLiteral("a wallet payment settles at the exact total"), pPaid.isOk(),
          pPaid.isErr() ? pPaid.error() : QString());
    if (pPaid.isOk()) {
        fx.expectedRevenuePaisa += pBill.total().paisa();
        tallyOrder(ctx, pid, fx.soldQty);
    }

    // A bogus code must be ignored, never applied and never fatal.
    auto m4 = ctx.orders().createOrder(models::OrderType::Delivery, 0, 0, 0);
    if (m4.isOk()) {
        const int bid = m4.value().id();
        ctx.orders().addItem(bid, fx.dishes[0].id(), 1);
        if (serveOrder(ctx, bid, why)) {
            auto bogus = ctx.billing().prepareBill(bid, QStringLiteral("NOPE-NOT-A-CODE"), 0);
            check(QStringLiteral("an unknown promo code is ignored, not applied"),
                  bogus.isOk() && bogus.value().discount().isZero(),
                  bogus.isErr() ? bogus.error() : bogus.value().discount().toString());
            if (bogus.isOk()) {
                models::Bill bb = bogus.value();
                auto bp = ctx.billing().settle(bid, bb, models::PaymentMethod::Cash,
                                               bb.total(), 1);
                if (bp.isOk()) {
                    fx.expectedRevenuePaisa += bb.total().paisa();
                    tallyOrder(ctx, bid, fx.soldQty);
                }
            }
        }
    }
}

// =============================================================================
// 3b. Empty / default-constructed text on NOT NULL columns
// =============================================================================

/**
 * A default-constructed QString is *null*, not empty, and Qt's SQL layer binds a null QString as
 * SQL NULL. Several `TEXT NOT NULL DEFAULT ''` columns are written from exactly such a QString —
 * an order with no note, a walk-in booking with no special request, a delivery with no note.
 * A column DEFAULT only fires when the column is OMITTED from an INSERT; it never rescues an
 * explicitly bound NULL, and it does nothing at all on an UPDATE. So each of these is a real
 * write that must land as the empty string.
 *
 * Every assertion below runs against the schema EXACTLY AS SHIPPED — the first three checks prove
 * that, by reading the live DDL back out of `sqlite_master` and insisting the constraints are
 * still there.
 */
void testEmptyText(services::AppContext& ctx, Fixture& fx) {
    section(QStringLiteral("EMPTY TEXT ON *NOT NULL* COLUMNS"));

    const QString ordersDdl = squeezed(scalarString(
        QStringLiteral("SELECT sql FROM sqlite_master WHERE type='table' AND name = ?"),
        {QStringLiteral("orders")}));
    const QString resvDdl = squeezed(scalarString(
        QStringLiteral("SELECT sql FROM sqlite_master WHERE type='table' AND name = ?"),
        {QStringLiteral("reservations")}));
    const QString usersDdl = squeezed(scalarString(
        QStringLiteral("SELECT sql FROM sqlite_master WHERE type='table' AND name = ?"),
        {QStringLiteral("users")}));

    check(QStringLiteral("the LIVE schema still declares orders.note TEXT NOT NULL"),
          ordersDdl.contains(QStringLiteral("note TEXT NOT NULL")), ordersDdl.left(200));
    check(QStringLiteral("the LIVE schema still declares reservations.special_request "
                         "TEXT NOT NULL"),
          resvDdl.contains(QStringLiteral("special_request TEXT NOT NULL")), resvDdl.left(200));
    check(QStringLiteral("the LIVE schema still declares users.remember_token TEXT NOT NULL"),
          usersDdl.contains(QStringLiteral("remember_token TEXT NOT NULL")), usersDdl.left(200));

    // --- an order with NO note ---------------------------------------------
    auto blank = ctx.orders().createOrder(models::OrderType::Takeaway, 0, 0, 0);
    check(QStringLiteral("AN ORDER WITH AN EMPTY NOTE IS CREATED"), blank.isOk(),
          blank.isErr() ? blank.error() : QString());
    if (blank.isOk()) {
        const int id = blank.value().id();
        check(QStringLiteral("the in-memory order really did carry a NULL QString note"),
              blank.value().note().isNull());
        checkEq(QStringLiteral("...yet the column holds '' and is NOT NULL on disk"),
                scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM orders WHERE id = ? "
                                              "AND note IS NOT NULL AND note = ''"), {id}),
                1);
        const auto rt = ctx.orders().order(id);
        check(QStringLiteral("...and the empty note ROUND-TRIPS back as an empty string"),
              rt.has_value() && rt->note().isEmpty(),
              rt ? rt->note() : QStringLiteral("(order did not reload)"));

        // The UPDATE path — the one a column DEFAULT can never rescue.
        auto cleared = ctx.orders().setOrderNote(id, QString());
        check(QStringLiteral("setOrderNote(QString()) succeeds — the UPDATE path, where a column "
                             "DEFAULT is no help at all"),
              cleared.isOk(), cleared.isErr() ? cleared.error() : QString());
        checkEq(QStringLiteral("...and the column is STILL '' and NOT NULL after that UPDATE"),
                scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM orders WHERE id = ? "
                                              "AND note IS NOT NULL AND note = ''"), {id}),
                1);

        const QString real = QStringLiteral("no chilli, extra achar");
        check(QStringLiteral("a real note is stored"), ctx.orders().setOrderNote(id, real).isOk());
        check(QStringLiteral("...and round-trips verbatim"),
              ctx.orders().order(id)->note() == real, ctx.orders().order(id)->note());
        check(QStringLiteral("...and can be cleared back to empty again"),
              ctx.orders().setOrderNote(id, QString()).isOk()
                  && ctx.orders().order(id)->note().isEmpty());

        // Do not leave an Open order sitting on the books for the reports suite to count.
        check(QStringLiteral("the note fixture order is cancelled cleanly"),
              ctx.orders().cancelOrder(id).isOk());
    }

    // --- a reservation with NO special request ------------------------------
    models::Reservation walkIn;
    walkIn.setTableId(fx.tableB);
    walkIn.setCustomerName(QStringLiteral("Walk-in Caller"));
    walkIn.setPhone(QStringLiteral("+977-9800123456"));
    walkIn.setStartsAt(QDateTime::currentDateTimeUtc().addSecs(30 * 3600));
    walkIn.setDurationMin(60);
    walkIn.setGuests(2);
    check(QStringLiteral("a default-constructed Reservation carries a NULL QString "
                         "special request"),
          walkIn.specialRequest().isNull());

    auto booked = ctx.reservations().book(walkIn);
    check(QStringLiteral("A RESERVATION WITH AN EMPTY SPECIAL REQUEST IS BOOKED"), booked.isOk(),
          booked.isErr() ? booked.error() : QString());
    if (booked.isOk()) {
        checkEq(QStringLiteral("...and special_request landed as '' rather than SQL NULL"),
                scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM reservations WHERE id = ? "
                                              "AND special_request IS NOT NULL "
                                              "AND special_request = ''"),
                               {booked.value()}),
                1);
    }

    // --- a restock booked in with no delivery note --------------------------
    const std::vector<models::Ingredient> stock = ctx.inventory().all();
    check(QStringLiteral("the inventory has stock items to book a delivery against"),
          !stock.empty());
    if (!stock.empty()) {
        const int ing = stock.front().id();
        const double before = stock.front().stockQty();
        auto delivery = ctx.inventory().restock(ing, 5.0, Money::fromRupees(120));
        check(QStringLiteral("a restock with NO delivery note is booked in"), delivery.isOk(),
              delivery.isErr() ? delivery.error() : QString());
        if (delivery.isOk()) {
            checkEq(QStringLiteral("...and the ledger row's note is '' rather than SQL NULL"),
                    scalarLongLong(
                        QStringLiteral("SELECT COUNT(*) FROM inventory_transactions "
                                       "WHERE ingredient_id = ? AND reason = 'RESTOCK' "
                                       "AND note IS NOT NULL AND note = ''"), {ing}),
                    1);
            double after = -1.0;
            for (const models::Ingredient& i : ctx.inventory().all())
                if (i.id() == ing) after = i.stockQty();
            checkNear(QStringLiteral("...and the stock really rose by the delivered quantity"),
                      after - before, 5.0, 1e-6);
        }
    }
}

// =============================================================================
// 4. Inventory
// =============================================================================

void testInventory(services::AppContext& ctx, Fixture& fx) {
    section(QStringLiteral("INVENTORY"));

    auto created = ctx.orders().createOrder(models::OrderType::DineIn, fx.tableB, 0, 0);
    if (created.isErr()) { check(QStringLiteral("inventory order created"), false, created.error()); return; }
    const int id = created.value().id();
    ctx.orders().addItem(id, fx.dishes[0].id(), 2);
    ctx.orders().addItem(id, fx.dishes[1].id(), 3);

    auto order = ctx.orders().order(id);
    if (!order) { check(QStringLiteral("inventory order reloads"), false); return; }

    const std::map<int, double> planned = plannedDraw(ctx, *order);
    check(QStringLiteral("the ordered dishes actually carry recipes"), !planned.empty(),
          QStringLiteral("%1 ingredients planned").arg(planned.size()));

    const std::map<int, double> before = stockSnapshot(ctx);

    QString why;
    check(QStringLiteral("moving the order to SERVED succeeds"), serveOrder(ctx, id, why), why);

    const std::map<int, double> after = stockSnapshot(ctx);

    bool allDeducted = true;
    QString firstWrong;
    for (const auto& entry : planned) {
        const auto b = before.find(entry.first);
        const auto a = after.find(entry.first);
        if (b == before.end() || a == after.end()) { allDeducted = false; continue; }
        const double expected = b->second - entry.second;
        if (std::fabs(a->second - expected) > 1e-6) {
            allDeducted = false;
            if (firstWrong.isEmpty())
                firstWrong = QStringLiteral("ingredient %1: %2 -> %3, expected %4")
                                 .arg(entry.first).arg(b->second).arg(a->second).arg(expected);
        }
    }
    check(QStringLiteral("SERVED deducts every recipe ingredient by recipe qty x ordered qty"),
          allDeducted, firstWrong);

    // Spot-check one ingredient numerically so a wholesale no-op cannot hide behind the loop.
    if (!planned.empty()) {
        const auto first = planned.begin();
        checkNear(QStringLiteral("spot check: ingredient %1 fell by exactly %2")
                      .arg(first->first).arg(first->second),
                  before.at(first->first) - after.at(first->first), first->second, 1e-6);
    }

    // The ledger must have a row per ingredient drawn.
    const long long rows =
        scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM inventory_transactions "
                                      "WHERE ref_order_id = ? AND reason = 'USAGE'"), {id});
    checkEq(QStringLiteral("inventory_transactions rows are written (one USAGE row per ingredient)"),
            rows, static_cast<long long>(planned.size()));

    QSqlQuery sum = persistence::Database::instance().prepared(
        QStringLiteral("SELECT COALESCE(SUM(delta_qty),0) FROM inventory_transactions "
                       "WHERE ref_order_id = ? AND reason = 'USAGE'"), {id});
    double plannedTotal = 0.0;
    for (const auto& e : planned) plannedTotal += e.second;
    if (sum.next())
        checkNear(QStringLiteral("the ledger deltas are NEGATIVE and sum to the planned draw"),
                  sum.value(0).toDouble(), -plannedTotal, 1e-6);
    else
        check(QStringLiteral("the ledger deltas are NEGATIVE and sum to the planned draw"), false,
              QStringLiteral("no ledger rows"));

    // --- refusing to drive stock negative ----------------------------------
    // Build an in-memory order asking for a physically impossible quantity of a dish
    // that has a recipe, and hand it straight to the inventory service.
    {
        models::Order absurd;
        absurd.addItem(fx.dishes[0].id(), fx.dishes[0].name(), fx.dishes[0].price(), 1000000);
        checkThrows<aluchop::core::InventoryException>(
            QStringLiteral("serving an order that would drive stock negative throws "
                           "InventoryException"),
            [&ctx, &absurd] { ctx.inventory().deductForOrder(absurd); });
    }

    // ...and nothing was drawn down by the refusal (all-or-nothing).
    const std::map<int, double> afterRefusal = stockSnapshot(ctx);
    bool untouched = true;
    for (const auto& e : after)
        if (afterRefusal.count(e.first) && std::fabs(afterRefusal.at(e.first) - e.second) > 1e-9)
            untouched = false;
    check(QStringLiteral("...and the refused deduction changed no stock at all (all-or-nothing)"),
          untouched);

    // A recipe pointing at a vanished ingredient is the other InventoryException path.
    {
        // Hand-build an order line for a dish whose recipe references a non-existent ingredient.
        const long long freeMenuId =
            scalarLongLong(QStringLiteral("SELECT id FROM menu_items ORDER BY id DESC LIMIT 1"), {});
        const long long ghostIngredient = 999999;
        long long inserted = 0;
        try {
            // The schema's foreign key may (correctly) forbid this fixture; that is not a defect,
            // it simply means this particular exception path cannot be provoked from outside.
            persistence::Database::instance().prepared(
                QStringLiteral("INSERT OR IGNORE INTO recipes (menu_item_id, ingredient_id, "
                               "qty_per_serving) VALUES (?, ?, ?)"),
                {static_cast<int>(freeMenuId), static_cast<int>(ghostIngredient), 0.1});
            inserted =
                scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM recipes WHERE menu_item_id = ? "
                                              "AND ingredient_id = ?"),
                               {static_cast<int>(freeMenuId), static_cast<int>(ghostIngredient)});
        } catch (const aluchop::core::AluChopException&) {
            inserted = 0;
        }
        if (inserted > 0) {
            models::Order ghosted;
            ghosted.addItem(static_cast<int>(freeMenuId), QStringLiteral("ghost dish"),
                            Money::fromRupees(100), 1);
            checkThrows<aluchop::core::InventoryException>(
                QStringLiteral("a recipe pointing at a missing ingredient throws "
                               "InventoryException"),
                [&ctx, &ghosted] { ctx.inventory().deductForOrder(ghosted); });
            persistence::Database::instance().prepared(
                QStringLiteral("DELETE FROM recipes WHERE menu_item_id = ? AND ingredient_id = ?"),
                {static_cast<int>(freeMenuId), static_cast<int>(ghostIngredient)});
        } else {
            out(QStringLiteral("  SKIP  missing-ingredient path (foreign key blocked the fixture)"));
        }
    }

    fx.outstandingOrders += 0;   // the inventory order ended Served, not outstanding
}

// =============================================================================
// 5. Split / merge
// =============================================================================

void testSplitMerge(services::AppContext& ctx, Fixture& fx) {
    section(QStringLiteral("SPLIT / MERGE"));

    // ------------------------------------------------------------------ split
    auto created = ctx.orders().createOrder(models::OrderType::DineIn, fx.tableA, 0, 0);
    if (created.isErr()) { check(QStringLiteral("split order created"), false, created.error()); return; }
    const int id = created.value().id();
    ctx.orders().addItem(id, fx.dishes[0].id(), 2);
    ctx.orders().addItem(id, fx.dishes[1].id(), 1);
    ctx.orders().addItem(id, fx.dishes[2].id(), 3);

    auto original = ctx.orders().order(id);
    if (!original) { check(QStringLiteral("split order reloads"), false); return; }
    const long long originalSubtotal = original->subtotal().paisa();
    const std::size_t originalLines = original->itemCount();
    std::map<QString, int> originalQty;
    for (const models::OrderItem& li : original->items()) originalQty[li.name()] += li.qty();

    auto split = ctx.orders().splitOrder(id, {0});
    check(QStringLiteral("a bill can be split"), split.isOk(),
          split.isErr() ? split.error() : QString());
    if (split.isErr()) return;
    const int newId = split.value().id();
    check(QStringLiteral("the breakaway is a NEW persisted order with its own id/number"),
          newId > 0 && newId != id
              && split.value().orderNumber() != original->orderNumber());

    auto left = ctx.orders().order(id);
    auto right = ctx.orders().order(newId);
    check(QStringLiteral("both halves reload from the database"), left && right);
    if (!left || !right) return;

    checkEq(QStringLiteral("the two halves hold every original line between them"),
            static_cast<long long>(left->itemCount() + right->itemCount()),
            static_cast<long long>(originalLines));
    checkEq(QStringLiteral("SPLIT TOTALS SUM BACK to the original subtotal"),
            left->subtotal().paisa() + right->subtotal().paisa(), originalSubtotal);

    std::map<QString, int> splitQty;
    for (const models::OrderItem& li : left->items()) splitQty[li.name()] += li.qty();
    for (const models::OrderItem& li : right->items()) splitQty[li.name()] += li.qty();
    check(QStringLiteral("...and every dish and quantity survives the split"),
          splitQty == originalQty);
    check(QStringLiteral("at least one line stays on the original bill"), left->itemCount() > 0);
    check(QStringLiteral("the breakaway carries the selected line"), right->itemCount() == 1);

    auto tooMuch = ctx.orders().splitOrder(id, {0, 1});
    check(QStringLiteral("splitting away EVERY line is refused"), tooMuch.isErr(),
          tooMuch.isOk() ? QStringLiteral("the original was emptied") : tooMuch.error());

    // ----------------------------------------------------- merge (model level)
    {
        models::Order a;
        models::Order b;
        a.addItem(101, QStringLiteral("Momo"), Money::fromRupees(400), 2);
        b.addItem(101, QStringLiteral("Momo"), Money::fromRupees(400), 3);
        b.addItem(102, QStringLiteral("Chowmein"), Money::fromRupees(250), 1);
        const long long bSubtotalBefore = b.subtotal().paisa();
        const std::size_t bLinesBefore = b.itemCount();

        a += b;

        checkEq(QStringLiteral("operator+= combines duplicate dishes into ONE line"),
                static_cast<long long>(a.itemCount()), 2);
        long long momo = 0;
        for (const models::OrderItem& li : a.items())
            if (li.menuItemId() == 101) momo = li.qty();
        checkEq(QStringLiteral("...with the quantities summed (2 + 3 == 5)"), momo, 5);
        checkEq(QStringLiteral("...and the merged subtotal is the sum of both"),
                a.subtotal().paisa(), 2 * 40000 + 3 * 40000 + 25000);
        checkEq(QStringLiteral("THE SOURCE IS UNCHANGED by a merge into a target (line count)"),
                static_cast<long long>(b.itemCount()), static_cast<long long>(bLinesBefore));
        checkEq(QStringLiteral("...and its subtotal is untouched"),
                b.subtotal().paisa(), bSubtotalBefore);
    }

    // --------------------------------------------------- merge (service level)
    auto tgt = ctx.orders().createOrder(models::OrderType::DineIn, fx.tableB, 0, 0);
    auto src = ctx.orders().createOrder(models::OrderType::Takeaway, 0, 0, 0);
    if (tgt.isErr() || src.isErr()) {
        check(QStringLiteral("merge fixtures created"), false,
              tgt.isErr() ? tgt.error() : src.error());
        return;
    }
    const int tid = tgt.value().id();
    const int sid = src.value().id();
    ctx.orders().addItem(tid, fx.dishes[0].id(), 2);
    ctx.orders().addItem(sid, fx.dishes[0].id(), 3);
    ctx.orders().addItem(sid, fx.dishes[1].id(), 1);

    const long long tSub = ctx.orders().order(tid)->subtotal().paisa();
    const long long sSub = ctx.orders().order(sid)->subtotal().paisa();
    std::map<QString, int> sourceQtyBefore;
    tallyOrder(ctx, sid, sourceQtyBefore);

    auto merged = ctx.orders().mergeOrders(tid, sid);
    check(QStringLiteral("two orders can be merged"), merged.isOk(),
          merged.isErr() ? merged.error() : QString());
    if (merged.isErr()) return;

    auto mergedTarget = ctx.orders().order(tid);
    auto mergedSource = ctx.orders().order(sid);
    check(QStringLiteral("both orders reload after the merge"), mergedTarget && mergedSource);
    if (!mergedTarget || !mergedSource) return;

    checkEq(QStringLiteral("the merged target holds ONE line per distinct dish"),
            static_cast<long long>(mergedTarget->itemCount()), 2);
    long long combined = 0;
    for (const models::OrderItem& li : mergedTarget->items())
        if (li.menuItemId() == fx.dishes[0].id()) combined = li.qty();
    checkEq(QStringLiteral("MERGE COMBINES the duplicate dish into one line with summed qty (2+3)"),
            combined, 5);
    checkEq(QStringLiteral("the merged subtotal equals the sum of the two originals"),
            mergedTarget->subtotal().paisa(), tSub + sSub);

    check(QStringLiteral("the source order is voided, not deleted"),
          mergedSource->status() == models::OrderStatus::Cancelled);
    std::map<QString, int> sourceQtyAfter;
    for (const models::OrderItem& li : mergedSource->items())
        sourceQtyAfter[li.name()] += li.qty();
    check(QStringLiteral("THE SOURCE ORDER'S OWN LINES ARE UNCHANGED by the merge"),
          sourceQtyAfter == sourceQtyBefore);
    checkEq(QStringLiteral("...and merged_into records where they went"),
            scalarLongLong(QStringLiteral("SELECT merged_into FROM orders WHERE id = ?"), {sid}),
            tid);

    auto self = ctx.orders().mergeOrders(tid, tid);
    check(QStringLiteral("an order cannot be merged into itself"), self.isErr());

    // These orders are left Open, so they are not outstanding kitchen work.
}

// =============================================================================
// 6. Reservations
// =============================================================================

void testReservations(services::AppContext& ctx, Fixture& fx) {
    section(QStringLiteral("RESERVATIONS"));

    const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(3 * 3600);

    models::Reservation first;
    first.setTableId(fx.tableA);
    first.setCustomerName(QStringLiteral("Anita Shrestha"));
    first.setPhone(QStringLiteral("+977-9800111222"));
    first.setStartsAt(base);
    first.setDurationMin(90);
    first.setGuests(2);

    auto booked = ctx.reservations().book(first);
    check(QStringLiteral("a table can be booked for a future window"), booked.isOk(),
          booked.isErr() ? booked.error() : QString());

    models::Reservation clash = first;
    clash.setCustomerName(QStringLiteral("Bijay Thapa"));
    clash.setStartsAt(base.addSecs(3600));            // starts 1h into the 90-minute sitting
    auto refused = ctx.reservations().book(clash);
    // Refused *for the right reason*: a refusal caused by some unrelated failure (e.g. the
    // insert blowing up) must not be allowed to masquerade as a working double-booking guard.
    check(QStringLiteral("DOUBLE-BOOKING the same table in an OVERLAPPING window is REFUSED"),
          refused.isErr() && refused.error().contains(QStringLiteral("already held")),
          refused.isOk() ? QStringLiteral("the table was double-booked") : refused.error());

    models::Reservation later = first;
    later.setCustomerName(QStringLiteral("Chandra Magar"));
    later.setStartsAt(base.addSecs(2 * 3600));        // starts 30 min after the first one ends
    auto accepted = ctx.reservations().book(later);
    check(QStringLiteral("a NON-OVERLAPPING booking on the SAME table is ACCEPTED"),
          accepted.isOk(), accepted.isErr() ? accepted.error() : QString());

    // The half-open rule: a booking starting exactly when another ends must fit.
    models::Reservation backToBack = first;
    backToBack.setCustomerName(QStringLiteral("Dipesh Rai"));
    backToBack.setStartsAt(base.addSecs(90 * 60));    // exactly at the first booking's end
    backToBack.setDurationMin(30);
    auto edge = ctx.reservations().book(backToBack);
    check(QStringLiteral("a back-to-back booking starting exactly at the previous end is accepted"),
          edge.isOk(), edge.isErr() ? edge.error() : QString());

    models::Reservation past = first;
    past.setCustomerName(QStringLiteral("Time Traveller"));
    past.setStartsAt(QDateTime::currentDateTimeUtc().addSecs(-7200));
    auto inThePast = ctx.reservations().book(past);
    check(QStringLiteral("a booking in the past is refused"), inThePast.isErr());

    const std::vector<models::Table> tables = ctx.reservations().tables();
    int smallTable = 0;
    for (const models::Table& t : tables)
        if (t.isActive() && t.capacity() <= 2 && smallTable == 0 && t.id() != fx.tableA)
            smallTable = t.id();

    if (smallTable != 0) {
        models::Reservation tooBig = first;
        tooBig.setTableId(smallTable);
        tooBig.setCustomerName(QStringLiteral("Party of Twelve"));
        tooBig.setGuests(12);
        tooBig.setStartsAt(base.addSecs(6 * 3600));
        auto overCapacity = ctx.reservations().book(tooBig);
        check(QStringLiteral("a party larger than the table's capacity is refused"),
              overCapacity.isErr() && overCapacity.error().contains(QStringLiteral("too small")),
              overCapacity.isOk() ? QStringLiteral("over-capacity booking accepted")
                                  : overCapacity.error());
    }

    // Availability is only a meaningful question once a booking actually exists, so this is
    // asserted against the window the FIRST booking holds — and only when that booking landed.
    if (booked.isOk()) {
        const std::vector<models::Table> free = ctx.reservations().availableTables(base, 90, 2);
        bool heldTableListed = false;
        bool anyOtherListed = false;
        for (const models::Table& t : free) {
            if (t.id() == fx.tableA) heldTableListed = true;
            else anyOtherListed = true;
        }
        check(QStringLiteral("availability OMITS the table already held for that window"),
              !heldTableListed,
              heldTableListed ? QStringLiteral("a booked table was offered as free") : QString());
        check(QStringLiteral("...while still offering the other free tables"), anyOtherListed);
    } else {
        check(QStringLiteral("availability can be evaluated (a booking exists to block a table)"),
              false, QStringLiteral("precondition failed: the first booking never persisted"));
    }
}

// =============================================================================
// 7. File handling (SPEC §5.6)
// =============================================================================

void testFileHandling(services::AppContext& ctx, const Fixture& fx) {
    section(QStringLiteral("FILE HANDLING (binary random access, CSV, PDF)"));

    const QString trailPath = QDir(fx.dataDir).filePath(QStringLiteral("e2e_trail.bin"));
    QFile::remove(trailPath);

    const int kRecords = 6;
    std::vector<QString> details;
    for (int i = 0; i < kRecords; ++i)
        details.push_back(QStringLiteral("record-%1-payload").arg(i));

    // --- write, then read back BY RANDOM ACCESS ----------------------------
    {
        persistence::AuditTrail trail(trailPath);
        for (int i = 0; i < kRecords; ++i) {
            const std::uint32_t seq =
                trail.record(static_cast<std::uint32_t>(100 + i),
                             QStringLiteral("ACT%1").arg(i),
                             QStringLiteral("ent:%1").arg(i),
                             Money(1000 + i), details[static_cast<std::size_t>(i)]);
            if (i == 0)
                checkEq(QStringLiteral("the first record gets sequence number 1"),
                        static_cast<long long>(seq), 1);
        }
        checkEq(QStringLiteral("%1 audit records were written").arg(kRecords),
                static_cast<long long>(trail.size()), kRecords);

        // Read out of order — index 4, then 1, then 0 — which is only possible with a seek.
        const std::vector<std::size_t> probeOrder = {4, 1, 0, 5, 3};
        bool allMatch = true;
        QString firstBad;
        for (const std::size_t idx : probeOrder) {
            const persistence::AuditRecord rec = trail.at(idx);
            const QString gotDetails = QString::fromLatin1(rec.details);
            const QString gotAction = QString::fromLatin1(rec.action);
            const bool match = gotDetails == details[idx]
                               && gotAction == QStringLiteral("ACT%1").arg(idx)
                               && rec.userId == static_cast<std::uint32_t>(100 + idx)
                               && rec.seq == static_cast<std::uint32_t>(idx + 1)
                               && rec.amountPaisa == static_cast<std::int64_t>(1000 + idx);
            if (!match && firstBad.isEmpty())
                firstBad = QStringLiteral("index %1: action '%2', details '%3', seq %4")
                               .arg(idx).arg(gotAction, gotDetails).arg(rec.seq);
            allMatch = allMatch && match;
        }
        check(QStringLiteral("RANDOM ACCESS: seeking straight to records 4,1,0,5,3 returns "
                             "exactly the right contents"),
              allMatch, firstBad);

        checkThrows<aluchop::core::FileIOException>(
            QStringLiteral("reading past the end of the trail throws FileIOException"),
            [&trail] { trail.at(999); });

        std::size_t bad = 12345;
        check(QStringLiteral("integrity verification PASSES on a good file"),
              trail.verifyIntegrity(bad), QStringLiteral("first bad index reported: %1").arg(bad));
    }   // the trail's fstream is closed here by its destructor

    // --- deliberately corrupt record 3, then re-verify ---------------------
    const std::size_t victim = 3;
    {
        std::fstream raw(trailPath.toStdString(),
                         std::ios::in | std::ios::out | std::ios::binary);
        check(QStringLiteral("the trail file can be reopened raw for tampering"), raw.is_open());
        if (raw.is_open()) {
            // Flip a byte inside the details field so magic still matches but the checksum cannot.
            const std::streamoff off =
                static_cast<std::streamoff>(victim * sizeof(persistence::AuditRecord))
                + static_cast<std::streamoff>(offsetof(persistence::AuditRecord, details));
            raw.seekp(off, std::ios::beg);
            const char tampered = 'Z';
            raw.write(&tampered, 1);
            raw.flush();
            raw.close();
        }
    }

    {
        persistence::AuditTrail reopened(trailPath);
        std::size_t firstBad = 99999;
        const bool clean = reopened.verifyIntegrity(firstBad);
        check(QStringLiteral("integrity verification DETECTS the tampered record"), !clean,
              clean ? QStringLiteral("corruption went unnoticed!") : QString());
        checkEq(QStringLiteral("...and reports the RIGHT index (%1)").arg(victim),
                static_cast<long long>(firstBad), static_cast<long long>(victim));
        checkThrows<aluchop::core::FileIOException>(
            QStringLiteral("reading the corrupted record directly also throws"),
            [&reopened] { reopened.at(victim); });
    }

    // --- the LIVE application trail ----------------------------------------
    {
        std::size_t firstBad = 0;
        const std::size_t count = ctx.audit().trailRecordCount();
        check(QStringLiteral("the live application audit trail has records"), count > 0,
              QStringLiteral("%1 records").arg(count));
        check(QStringLiteral("the live application audit trail verifies clean"),
              ctx.audit().verifyTrailIntegrity(firstBad),
              QStringLiteral("first bad index %1").arg(firstBad));
        if (count > 0) {
            const persistence::AuditRecord first = ctx.audit().trailRecordAt(0);
            check(QStringLiteral("the first live record is APP_START, written at boot"),
                  QString::fromLatin1(first.action) == QStringLiteral("APP_START"),
                  QString::fromLatin1(first.action));
        }
    }

    // --- the raw <fstream> application log ---------------------------------
    // BillingService::settle() appends the operator<<-rendered archival receipt here, and every
    // failure the services layer swallows is reported here too. Both facts are asserted.
    {
        const QString logPath = QDir(fx.dataDir).filePath(QStringLiteral("logs/aluchop.log"));
        QFile log(logPath);
        check(QStringLiteral("the append-mode application log exists and has content"),
              log.exists() && log.size() > 0,
              QStringLiteral("%1 (%2 bytes)").arg(logPath).arg(log.size()));
        if (log.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromUtf8(log.readAll());
            log.close();
            check(QStringLiteral("every settled bill left an operator<<-rendered RECEIPT in the "
                                 "journal"),
                  text.contains(QStringLiteral("RECEIPT "))
                      && text.contains(QStringLiteral("All prices are inclusive of tax.")),
                  QStringLiteral("%1 chars of log").arg(text.size()));
            check(QStringLiteral("...with its money in the locale-independent NPR form"),
                  text.contains(QStringLiteral("NPR ")));
            check(QStringLiteral("the journal reports NO audit-mirror write failure"),
                  !text.contains(QStringLiteral("AUDIT MIRROR WRITE FAILED")),
                  QStringLiteral("the audit_log mirror rejected at least one row"));
            check(QStringLiteral("the journal reports NO 'NOT NULL constraint failed' anywhere"),
                  !text.contains(QStringLiteral("NOT NULL constraint failed")),
                  QStringLiteral("a null QString still reaches a NOT NULL column"));
            check(QStringLiteral("...and no login was aborted"),
                  !text.contains(QStringLiteral("login aborted")),
                  QStringLiteral("a login was aborted by a database failure"));
        } else {
            check(QStringLiteral("the append-mode application log can be read back"), false,
                  logPath);
        }
    }

    // --- CSV: quoting and escaping -----------------------------------------
    const QString csvPath = QDir(fx.dataDir).filePath(QStringLiteral("escaping.csv"));
    {
        persistence::CsvWriter w;
        w.open(csvPath);
        w.writeRow({QStringLiteral("plain"), QStringLiteral("with,comma"),
                    QStringLiteral("with\"quote"), QStringLiteral("with\nnewline"),
                    QStringLiteral("Rs 1,250.00")});
        w.close();
    }
    QFile csv(csvPath);
    check(QStringLiteral("CSV export writes a real file"),
          csv.exists() && csv.size() > 0,
          QStringLiteral("%1 bytes").arg(csv.size()));
    if (csv.open(QIODevice::ReadOnly)) {
        const QString text = QString::fromUtf8(csv.readAll());
        csv.close();
        const QString expected =
            QStringLiteral("plain,\"with,comma\",\"with\"\"quote\",\"with\nnewline\","
                           "\"Rs 1,250.00\"");
        check(QStringLiteral("CSV fields are RFC-4180 quoted and escaped exactly"),
              text.startsWith(expected), text.left(120));
        check(QStringLiteral("...a field with no special characters is left unquoted"),
              text.startsWith(QStringLiteral("plain,")));
    } else {
        check(QStringLiteral("CSV fields are RFC-4180 quoted and escaped exactly"), false,
              QStringLiteral("could not reopen the CSV"));
    }

    // --- a real report export ----------------------------------------------
    const QDate today = QDateTime::currentDateTimeUtc().date();
    const QString reportCsv = QDir(fx.dataDir).filePath(QStringLiteral("sales_report.csv"));
    {
        auto report = ctx.reports().makeReport(services::ReportKind::Sales, today, today);
        check(QStringLiteral("the report factory hands back a concrete Sales report"),
              report != nullptr);
        if (report) {
            const QString written = report->exportCsv(reportCsv);
            QFile f(written);
            check(QStringLiteral("SalesReport::exportCsv writes a real, non-empty CSV"),
                  f.exists() && f.size() > 0, QStringLiteral("%1 bytes").arg(f.size()));
            if (f.open(QIODevice::ReadOnly)) {
                const QString head = QString::fromUtf8(f.readLine());
                f.close();
                check(QStringLiteral("...whose first row is the column header"),
                      head.contains(QStringLiteral("Date")) && head.contains(QStringLiteral("Revenue")),
                      head.trimmed());
            }
        }
    }

    // --- PDF ----------------------------------------------------------------
    const QString pdfPath = QDir(fx.dataDir).filePath(QStringLiteral("sales_report.pdf"));
    {
        auto report = ctx.reports().makeReport(services::ReportKind::Sales, today, today);
        if (report) {
            auto res = gui::PdfExporter::exportReportPdf(report->title(), report->header(),
                                                         report->rows(), pdfPath);
            check(QStringLiteral("PDF export reports success"), res.isOk(),
                  res.isErr() ? res.error() : QString());
        }
    }
    QFile pdf(pdfPath);
    check(QStringLiteral("PDF export writes a real, non-empty file"),
          pdf.exists() && pdf.size() > 0, QStringLiteral("%1 bytes").arg(pdf.size()));
    if (pdf.open(QIODevice::ReadOnly)) {
        const QByteArray magic = pdf.read(4);
        pdf.close();
        check(QStringLiteral("...and it starts with the %PDF magic bytes"),
              magic == QByteArray("%PDF"), QString::fromLatin1(magic.toHex()));
    } else {
        check(QStringLiteral("...and it starts with the %PDF magic bytes"), false,
              QStringLiteral("could not open the PDF"));
    }
}

// =============================================================================
// 7b. The audit_log SQL mirror
// =============================================================================

/**
 * `AuditService::log()` writes twice: the authoritative 128-byte binary record first, then a row
 * in the queryable `audit_log` mirror. A mirror insert that fails is deliberately not fatal — but
 * it must not be invisible either, and it must not be the *normal* case. This suite asserts the
 * mirror is genuinely populated and genuinely agrees, record for record, with the binary trail.
 */
void testAuditMirror(services::AppContext& ctx) {
    section(QStringLiteral("AUDIT LOG (SQL mirror vs the binary trail)"));

    const long long rows = scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM audit_log"), {});
    check(QStringLiteral("AUDIT_LOG ACTUALLY RECEIVES ROWS"), rows > 0,
          QStringLiteral("%1 rows").arg(rows));

    const std::size_t trailCount = ctx.audit().trailRecordCount();
    checkEq(QStringLiteral("the mirror holds ONE ROW PER BINARY RECORD (nothing was dropped)"),
            rows, static_cast<long long>(trailCount));

    checkEq(QStringLiteral("no mirror row was written with the seq-0 'trail write failed' marker"),
            scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM audit_log WHERE seq = 0"), {}), 0);
    checkEq(QStringLiteral("every seq is distinct — the mirror never double-writes a record"),
            scalarLongLong(QStringLiteral("SELECT COUNT(DISTINCT seq) FROM audit_log"), {}), rows);
    checkEq(QStringLiteral("the seq numbers run 1..N with no gap"),
            scalarLongLong(QStringLiteral("SELECT MAX(seq) FROM audit_log"), {}), rows);
    checkEq(QStringLiteral("the defaulted `details` argument is stored as '', never as SQL NULL"),
            scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM audit_log "
                                          "WHERE details IS NULL OR action IS NULL "
                                          "OR entity IS NULL"), {}),
            0);
    check(QStringLiteral("...and plenty of those rows really are the empty-details kind"),
          scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM audit_log WHERE details = ''"), {})
              > 0);

    // The actions this run genuinely performed must all be on the record.
    const QStringList mustHave = {QStringLiteral("APP_START"), QStringLiteral("LOGIN"),
                                  QStringLiteral("LOGOUT"),    QStringLiteral("USER_CREATE"),
                                  QStringLiteral("PWD_RESET"), QStringLiteral("PWD_CHANGE"),
                                  QStringLiteral("ORDER_NEW"), QStringLiteral("ORDER_FIRE"),
                                  QStringLiteral("ORDER_PAID"), QStringLiteral("ORDER_SPLIT"),
                                  QStringLiteral("ORDER_MERGE"), QStringLiteral("ORDER_NOTE"),
                                  QStringLiteral("RESV_NEW"), QStringLiteral("RESTOCK"),
                                  QStringLiteral("STOCK_USED")};
    QStringList absent;
    for (const QString& action : mustHave)
        if (scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM audit_log WHERE action = ?"),
                           {action}) == 0)
            absent << action;
    check(QStringLiteral("every action this run performed is mirrored into audit_log"),
          absent.isEmpty(), QStringLiteral("missing: %1").arg(absent.join(QStringLiteral(", "))));

    // Money-bearing records carry their money.
    check(QStringLiteral("the ORDER_PAID rows carry the money that was taken"),
          scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM audit_log "
                                        "WHERE action = 'ORDER_PAID' AND amount_paisa > 0"), {})
              > 0);
    check(QStringLiteral("a signed-in action is stamped with the acting user, not 0"),
          scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM audit_log "
                                        "WHERE action = 'ORDER_PAID' AND user_id > 0"), {}) > 0);

    // --- random access: record #N by index, cross-checked against the mirror ---
    // trailRecordAt(index) seeks straight to byte index*128. Sample the ends and the middle.
    if (trailCount > 0) {
        std::vector<std::size_t> probes = {0, trailCount / 3, trailCount / 2, trailCount - 1};
        bool allAgree = true;
        QString firstBad;
        for (const std::size_t idx : probes) {
            const persistence::AuditRecord rec = ctx.audit().trailRecordAt(idx);
            const QString mirrorAction =
                scalarString(QStringLiteral("SELECT action FROM audit_log WHERE seq = ?"),
                             {static_cast<int>(rec.seq)});
            const QString mirrorEntity =
                scalarString(QStringLiteral("SELECT entity FROM audit_log WHERE seq = ?"),
                             {static_cast<int>(rec.seq)});
            const long long mirrorAmount =
                scalarLongLong(QStringLiteral("SELECT amount_paisa FROM audit_log WHERE seq = ?"),
                               {static_cast<int>(rec.seq)});
            const long long mirrorTs =
                scalarLongLong(QStringLiteral("SELECT ts_utc_ms FROM audit_log WHERE seq = ?"),
                               {static_cast<int>(rec.seq)});

            // The 16-byte on-disk fields are NUL-padded and truncated; the mirror is not, so the
            // honest comparison is "the mirror's text starts with what the record could hold".
            const bool agree =
                rec.seq == static_cast<std::uint32_t>(idx + 1)
                && rec.magic == persistence::kAuditRecordMagic
                && mirrorAction.left(15) == QString::fromLatin1(rec.action)
                && mirrorEntity.left(15) == QString::fromLatin1(rec.entity)
                && mirrorAmount == static_cast<long long>(rec.amountPaisa)
                && mirrorTs == static_cast<long long>(rec.timestampUtcMs);
            if (!agree && firstBad.isEmpty())
                firstBad = QStringLiteral("index %1: record seq %2 '%3'/'%4' %5 @%6 vs mirror "
                                          "'%7'/'%8' %9 @%10")
                               .arg(idx).arg(rec.seq)
                               .arg(QString::fromLatin1(rec.action),
                                    QString::fromLatin1(rec.entity))
                               .arg(static_cast<long long>(rec.amountPaisa))
                               .arg(static_cast<long long>(rec.timestampUtcMs))
                               .arg(mirrorAction, mirrorEntity)
                               .arg(mirrorAmount).arg(mirrorTs);
            allAgree = allAgree && agree;
        }
        check(QStringLiteral("RANDOM ACCESS on the LIVE trail: record #N by index is record N+1 "
                             "and agrees with its mirror row"),
              allAgree, firstBad);

        const persistence::AuditRecord last = ctx.audit().trailRecordAt(trailCount - 1);
        const persistence::AuditRecord first = ctx.audit().trailRecordAt(0);
        check(QStringLiteral("...and seeking backwards to record 0 after record N-1 still works"),
              first.seq == 1 && last.seq == static_cast<std::uint32_t>(trailCount),
              QStringLiteral("first seq %1, last seq %2").arg(first.seq).arg(last.seq));

        checkThrows<aluchop::core::FileIOException>(
            QStringLiteral("asking the live trail for a record past the end throws"),
            [&ctx, trailCount] { ctx.audit().trailRecordAt(trailCount + 50); });
    }

    // The mirror's own reader.
    const auto recent = ctx.audit().recentTrailRecords(5);
    check(QStringLiteral("the trail hands back its most recent records"), recent.size() == 5,
          QStringLiteral("%1 returned").arg(recent.size()));
    if (recent.size() == 5) {
        bool ascending = true;
        for (std::size_t i = 1; i < recent.size(); ++i)
            if (recent[i].seq <= recent[i - 1].seq) ascending = false;
        check(QStringLiteral("...oldest first, with strictly increasing sequence numbers"),
              ascending);
        checkEq(QStringLiteral("...ending at the newest record in the trail"),
                static_cast<long long>(recent.back().seq), static_cast<long long>(trailCount));
    }
}

// =============================================================================
// 8. Persistence round-trip
// =============================================================================

void testRoundTrip(services::AppContext& ctx, const Fixture& fx) {
    section(QStringLiteral("PERSISTENCE ROUND-TRIP"));

    if (fx.paidOrderId == 0) {
        check(QStringLiteral("a settled order is available for the round-trip"), false,
              QStringLiteral("no order was paid earlier"));
        return;
    }

    // Build the expectation from a completely independent path: raw SQL.
    const int id = fx.paidOrderId;
    QSqlQuery hdr = persistence::Database::instance().prepared(
        QStringLiteral("SELECT order_number, type, status, table_id, customer_id, waiter_id, note "
                       "FROM orders WHERE id = ?"), {id});
    check(QStringLiteral("the order header is on disk"), hdr.next());
    if (!hdr.isValid()) return;

    const auto reloaded = ctx.orders().order(id);
    check(QStringLiteral("the order rehydrates through the repository"), reloaded.has_value());
    if (!reloaded) return;

    check(QStringLiteral("order number survives the round-trip"),
          reloaded->orderNumber() == hdr.value(0).toString(), reloaded->orderNumber());
    check(QStringLiteral("order type survives the round-trip"),
          models::toString(reloaded->type()) == hdr.value(1).toString(),
          models::toString(reloaded->type()));
    check(QStringLiteral("STATUS-LADDER HYDRATION: a Paid order reloads as Paid "
                         "(no illegal-transition throw)"),
          reloaded->status() == models::OrderStatus::Paid
              && hdr.value(2).toString() == QStringLiteral("PAID"),
          models::toString(reloaded->status()));
    checkEq(QStringLiteral("table id survives the round-trip"),
            reloaded->tableId(), hdr.value(3).toInt());
    checkEq(QStringLiteral("customer id survives the round-trip"),
            reloaded->customerId(), hdr.value(4).toInt());
    checkEq(QStringLiteral("waiter id survives the round-trip"),
            reloaded->waiterId(), hdr.value(5).toInt());
    check(QStringLiteral("the note survives the round-trip"),
          reloaded->note() == hdr.value(6).toString());
    check(QStringLiteral("the creation timestamp survives the round-trip"),
          reloaded->createdAt().isValid());

    QSqlQuery lines = persistence::Database::instance().prepared(
        QStringLiteral("SELECT menu_item_id, name_snapshot, unit_price_paisa, qty "
                       "FROM order_items WHERE order_id = ? ORDER BY id"), {id});
    std::vector<std::tuple<int, QString, long long, int>> onDisk;
    while (lines.next())
        onDisk.emplace_back(lines.value(0).toInt(), lines.value(1).toString(),
                            lines.value(2).toLongLong(), lines.value(3).toInt());

    checkEq(QStringLiteral("every line item survives the round-trip (count)"),
            static_cast<long long>(reloaded->itemCount()),
            static_cast<long long>(onDisk.size()));

    bool linesMatch = onDisk.size() == reloaded->itemCount();
    QString firstBad;
    for (std::size_t i = 0; i < onDisk.size() && i < reloaded->itemCount(); ++i) {
        const models::OrderItem& li = (*reloaded)[i];
        const bool same = li.menuItemId() == std::get<0>(onDisk[i])
                          && li.name() == std::get<1>(onDisk[i])
                          && li.unitPrice().paisa() == std::get<2>(onDisk[i])
                          && li.qty() == std::get<3>(onDisk[i]);
        if (!same && firstBad.isEmpty())
            firstBad = QStringLiteral("line %1: got (%2, '%3', %4, %5)")
                           .arg(i).arg(li.menuItemId()).arg(li.name())
                           .arg(li.unitPrice().paisa()).arg(li.qty());
        linesMatch = linesMatch && same;
    }
    check(QStringLiteral("every line's dish id, NAME SNAPSHOT, unit price and qty survive"),
          linesMatch, firstBad);

    long long diskSubtotal = 0;
    for (const auto& l : onDisk) diskSubtotal += std::get<2>(l) * std::get<3>(l);
    checkEq(QStringLiteral("the rehydrated subtotal matches what is stored on disk"),
            reloaded->subtotal().paisa(), diskSubtotal);

    const long long paidTotal =
        scalarLongLong(QStringLiteral("SELECT total_paisa FROM payments WHERE order_id = ?"), {id});
    checkEq(QStringLiteral("the settled payment row records the same money"),
            paidTotal, diskSubtotal);
}

// =============================================================================
// 9. Reports
// =============================================================================

void testReports(services::AppContext& ctx, Fixture& fx) {
    section(QStringLiteral("REPORTS"));

    // ReportService's documented contract: every QDate crossing it is a LOCAL calendar date, and
    // a business day runs from local midnight to the next local midnight — which is exactly the
    // date the dashboard and the report pickers hand it (QDate::currentDate()).
    const QDate utcToday = QDate::currentDate();

    const Money today = ctx.reports().salesForDay(utcToday);
    checkEq(QStringLiteral("today's revenue equals the hand-computed sum of every settled bill"),
            today.paisa(), fx.expectedRevenuePaisa);

    // The day windows are correctly placed AND disjoint: everything this run settled falls in
    // today's window and nothing spills into the neighbouring days.
    checkEq(QStringLiteral("yesterday's window is empty (nothing leaked backwards over midnight)"),
            ctx.reports().salesForDay(utcToday.addDays(-1)).paisa(), 0);
    checkEq(QStringLiteral("tomorrow's window is empty (nothing leaked forwards over midnight)"),
            ctx.reports().salesForDay(utcToday.addDays(1)).paisa(), 0);
    checkEq(QStringLiteral("...so three adjacent day windows sum to the day's takings exactly once"),
            ctx.reports().salesForDay(utcToday.addDays(-1)).paisa() + today.paisa()
                + ctx.reports().salesForDay(utcToday.addDays(1)).paisa(),
            fx.expectedRevenuePaisa);
    checkEq(QStringLiteral("an INVALID date reads zero, not 'every payment ever written'"),
            ctx.reports().salesForDay(QDate()).paisa(), 0);

    const long long dbTotal =
        scalarLongLong(QStringLiteral("SELECT COALESCE(SUM(total_paisa),0) FROM payments"), {});
    checkEq(QStringLiteral("...and equals SUM(total_paisa) straight out of the payments table"),
            dbTotal, fx.expectedRevenuePaisa);

    const std::array<Money, 7> week = ctx.reports().weeklySales(utcToday);
    checkEq(QStringLiteral("the weekly series' last bucket is today"),
            week[6].paisa(), fx.expectedRevenuePaisa);
    long long weekSum = 0;
    for (const Money& m : week) weekSum += m.paisa();
    checkEq(QStringLiteral("the whole week sums to the same figure (nothing else was settled)"),
            weekSum, fx.expectedRevenuePaisa);

    const Money month = ctx.reports().salesForMonth(utcToday.year(), utcToday.month());
    check(QStringLiteral("the month total is at least today's revenue"),
          month.paisa() >= fx.expectedRevenuePaisa,
          QStringLiteral("month %1 vs today %2").arg(month.paisa()).arg(fx.expectedRevenuePaisa));

    const auto series = ctx.reports().revenueSeries(utcToday, utcToday);
    check(QStringLiteral("the revenue series returns one point for a one-day range"),
          series.size() == 1);
    if (series.size() == 1)
        checkEq(QStringLiteral("...whose value is today's revenue"),
                series[0].second.paisa(), fx.expectedRevenuePaisa);

    // Popular items: hand-computed from the dishes on the orders this test actually settled.
    const auto popular = ctx.reports().popularItems(50);
    long long popularTotal = 0;
    for (const auto& p : popular) popularTotal += p.second;
    long long expectedSold = 0;
    for (const auto& s : fx.soldQty) expectedSold += s.second;
    checkEq(QStringLiteral("popularItems' unit total equals the hand count over every settled "
                           "order"),
            popularTotal, expectedSold);
    checkEq(QStringLiteral("...spread over the same number of distinct dishes"),
            static_cast<long long>(popular.size()), static_cast<long long>(fx.soldQty.size()));
    check(QStringLiteral("...and it is ranked, most sold first"),
          std::is_sorted(popular.begin(), popular.end(),
                         [](const std::pair<QString, int>& a, const std::pair<QString, int>& b) {
                             return a.second > b.second;
                         }));

    bool everyDishRight = true;
    QString wrong;
    for (const auto& p : popular) {
        const auto it = fx.soldQty.find(p.first);
        const int want = it == fx.soldQty.end() ? 0 : it->second;
        if (p.second != want) {
            everyDishRight = false;
            if (wrong.isEmpty())
                wrong = QStringLiteral("'%1': reported %2, hand-counted %3")
                            .arg(p.first).arg(p.second).arg(want);
        }
    }
    check(QStringLiteral("every popular-item quantity matches the hand count"),
          everyDishRight, wrong);

    checkEq(QStringLiteral("the customer count matches the customers table"),
            ctx.reports().customerCount(),
            scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM customers"), {}));

    const long long outstanding =
        scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM orders WHERE status IN "
                                      "('PENDING','PREPARING','READY')"), {});
    checkEq(QStringLiteral("the pending-order count matches the kitchen's outstanding workload"),
            ctx.reports().pendingOrderCount(), outstanding);

    // The reports must not invent tax anywhere either.
    auto ordersReport = ctx.reports().makeReport(services::ReportKind::Orders, utcToday, utcToday);
    check(QStringLiteral("an Orders report can be generated"), ordersReport != nullptr);
    if (ordersReport) {
        const auto rows = ordersReport->rows();
        check(QStringLiteral("...and it contains the orders raised today"), !rows.empty(),
              QStringLiteral("%1 rows").arg(rows.size()));
        bool widthOk = true;
        for (const QStringList& r : rows)
            if (r.size() != ordersReport->header().size()) widthOk = false;
        check(QStringLiteral("...with every row matching the header width"), widthOk);
    }

    // The business day is the LOCAL one. This is the assertion that catches a regression back to
    // UTC-windowed days: the sales this run settled belong to the date that was on the wall when
    // they were paid for, i.e. the very QDate the dashboard passes in.
    check(QStringLiteral("the business day is the LOCAL calendar day the dashboard asks about"),
          ctx.reports().salesForDay(QDate::currentDate()).paisa() == fx.expectedRevenuePaisa,
          QStringLiteral("salesForDay(%1) = %2, hand count %3")
              .arg(QDate::currentDate().toString(Qt::ISODate))
              .arg(ctx.reports().salesForDay(QDate::currentDate()).paisa())
              .arg(fx.expectedRevenuePaisa));
    if (QDate::currentDate() != QDateTime::currentDateTimeUtc().date()) {
        out(QStringLiteral("  NOTE  local date (%1) differs from the UTC date (%2) right now — a "
                           "UTC-windowed 'day' would have misfiled every sale above")
                .arg(QDate::currentDate().toString(Qt::ISODate),
                     QDateTime::currentDateTimeUtc().date().toString(Qt::ISODate)));
    }

    // ---------------------------------------------------------------------
    // Deterministic probe for the trailing-window boundary.
    // A sale is settled and popularItems() is asked about it microseconds later. Both the
    // stored `paid_at` and the query's upper bound are whole-second ISO strings, and the
    // bound is EXCLUSIVE, so a sale made in the current second cannot satisfy
    // `paid_at < now` and disappears from the report until the clock ticks over.
    // ---------------------------------------------------------------------
    auto probe = ctx.orders().createOrder(models::OrderType::Takeaway, 0, 0, 0);
    if (probe.isOk()) {
        const int probeId = probe.value().id();
        const models::MenuItem& probeDish = fx.dishes[3];
        ctx.orders().addItem(probeId, probeDish.id(), 1);
        QString why;
        if (serveOrder(ctx, probeId, why)) {
            auto pb = ctx.billing().prepareBill(probeId);
            if (pb.isOk()) {
                models::Bill b = pb.value();
                auto settled = ctx.billing().settle(probeId, b, models::PaymentMethod::Cash,
                                                    b.total(), 1);
                check(QStringLiteral("the probe sale settles"), settled.isOk(),
                      settled.isErr() ? settled.error() : QString());
                if (settled.isOk()) {
                    // Revenue (whole-day window) sees it immediately...
                    checkEq(QStringLiteral("salesForDay sees a sale settled a moment ago"),
                            ctx.reports().salesForDay(utcToday).paisa(),
                            fx.expectedRevenuePaisa + b.total().paisa());

                    // ...but the trailing 30-day window, whose upper bound is "now", does not.
                    const auto fresh = ctx.reports().popularItems(50);
                    bool listed = false;
                    for (const auto& p : fresh)
                        if (p.first == probeDish.name()) listed = true;
                    check(QStringLiteral("popularItems sees a sale settled a moment ago"), listed,
                          QStringLiteral("'%1' is missing from the %2 rows popularItems returned "
                                         "— the trailing window's EXCLUSIVE upper bound is "
                                         "whole-second 'now', so sales made during the current "
                                         "second are dropped")
                              .arg(probeDish.name()).arg(fresh.size()));

                    if (!listed) {
                        // Prove the diagnosis in-process rather than inferring it: wait for the
                        // clock to tick past the second the payment was stamped with, change
                        // nothing else, and ask the very same question again.
                        std::this_thread::sleep_for(std::chrono::milliseconds(1300));
                        const auto later = ctx.reports().popularItems(50);
                        bool listedLater = false;
                        long long laterTotal = 0;
                        for (const auto& p : later) {
                            laterTotal += p.second;
                            if (p.first == probeDish.name()) listedLater = true;
                        }
                        check(QStringLiteral("CONFIRMED CAUSE: the identical query returns the "
                                             "sale one second later (the window bound, not the "
                                             "aggregation, is at fault)"),
                              listedLater && laterTotal > 0,
                              QStringLiteral("after the tick popularItems returned %1 rows / "
                                             "%2 units").arg(later.size()).arg(laterTotal));
                    }
                }
            }
        }
    }
}

// =============================================================================
// 10. The kitchen pass — the groupings the POS board reads
//
// The board renders three columns straight out of OrderService::withStatus(). If those
// groupings are wrong, overlapping or empty while tickets are genuinely outstanding, the GUI
// draws a plausible-looking but false kitchen. Nothing here touches a widget: it asserts the
// service contract the widget consumes.
// =============================================================================

void testKitchenPass(services::AppContext& ctx, Fixture& fx) {
    section(QStringLiteral("KITCHEN PASS (the board's groupings)"));

    /// The set of order ids the board would put in one column.
    const auto idsIn = [&ctx](models::OrderStatus s) {
        std::set<int> ids;
        for (const models::Order& o : ctx.orders().withStatus(s)) ids.insert(o.id());
        return ids;
    };

    // Three fresh tickets, each carrying real lines. The board prints an order number and dish
    // names, so a ticket with neither would make "never elide the identifier" a vacuous rule.
    std::vector<int> ticket;
    for (int i = 0; i < 3; ++i) {
        auto made = ctx.orders().createOrder(models::OrderType::Takeaway, 0, 0, 0);
        if (made.isErr()) {
            check(QStringLiteral("kitchen fixture: ticket %1 is created").arg(i + 1), false,
                  made.error());
            return;
        }
        const int id = made.value().id();
        ctx.orders().addItem(id, fx.dishes[static_cast<std::size_t>(i)].id(), i + 1);
        ticket.push_back(id);
    }
    check(QStringLiteral("three fresh tickets are raised for the pass"), ticket.size() == 3);

    bool allFired = true;
    QString fireError;
    for (int id : ticket) {
        const auto fired = ctx.orders().submitToKitchen(id);
        if (fired.isErr()) { allFired = false; if (fireError.isEmpty()) fireError = fired.error(); }
    }
    check(QStringLiteral("all three are fired to the kitchen (-> Pending)"), allFired, fireError);

    {
        const std::set<int> pending = idsIn(models::OrderStatus::Pending);
        check(QStringLiteral("the PENDING grouping is NOT empty while three tickets are pending"),
              !pending.empty());
        bool allListed = true;
        for (int id : ticket) if (!pending.count(id)) allListed = false;
        check(QStringLiteral("...and it lists every ticket that was just fired"), allListed);
    }

    // Drive the board into all three states at once: one stays Pending, one goes Preparing,
    // one goes Ready. This is the only arrangement in which all three columns are populated.
    check(QStringLiteral("ticket 2 is picked up (Pending -> Preparing)"),
          ctx.orders().advanceStatus(ticket[1]).isOk());
    check(QStringLiteral("ticket 3 is picked up and plated (Pending -> Preparing -> Ready)"),
          ctx.orders().advanceStatus(ticket[2]).isOk()
              && ctx.orders().advanceStatus(ticket[2]).isOk());

    const std::set<int> pending = idsIn(models::OrderStatus::Pending);
    const std::set<int> preparing = idsIn(models::OrderStatus::Preparing);
    const std::set<int> ready = idsIn(models::OrderStatus::Ready);

    check(QStringLiteral("PENDING is non-empty and holds the ticket still waiting"),
          !pending.empty() && pending.count(ticket[0]) == 1,
          QStringLiteral("%1 pending").arg(pending.size()));
    check(QStringLiteral("PREPARING is non-empty and holds the ticket on the stove"),
          !preparing.empty() && preparing.count(ticket[1]) == 1,
          QStringLiteral("%1 preparing").arg(preparing.size()));
    check(QStringLiteral("READY is non-empty and holds the plated ticket"),
          !ready.empty() && ready.count(ticket[2]) == 1,
          QStringLiteral("%1 ready").arg(ready.size()));

    // A ticket may sit in exactly one column. Two columns claiming the same order is the
    // kitchen equivalent of two numbers on one screen contradicting each other.
    bool disjoint = true;
    for (int id : pending) if (preparing.count(id) || ready.count(id)) disjoint = false;
    for (int id : preparing) if (ready.count(id)) disjoint = false;
    check(QStringLiteral("the three columns are DISJOINT — no ticket is shown twice"), disjoint);

    // Every row the board draws must genuinely carry the status of the column it sits in.
    const std::array<models::OrderStatus, 3> board{models::OrderStatus::Pending,
                                                   models::OrderStatus::Preparing,
                                                   models::OrderStatus::Ready};
    bool labelsHonest = true;
    bool identifiersPresent = true;
    QString offender;
    long long boardRows = 0;
    for (models::OrderStatus s : board) {
        for (const models::Order& o : ctx.orders().withStatus(s)) {
            ++boardRows;
            if (o.status() != s) {
                labelsHonest = false;
                if (offender.isEmpty())
                    offender = QStringLiteral("order %1 filed under %2 but is %3")
                                   .arg(o.id()).arg(models::toString(s), models::toString(o.status()));
            }
            if (o.orderNumber().trimmed().isEmpty()) {
                identifiersPresent = false;
                if (offender.isEmpty())
                    offender = QStringLiteral("order %1 has no ORD- number").arg(o.id());
            }
        }
    }
    check(QStringLiteral("every ticket really carries the status of the column it sits in"),
          labelsHonest, offender);
    check(QStringLiteral("every ticket on the board carries a printable order number"),
          identifiersPresent, offender);

    // Our three tickets have lines, so the board has dish names to render (and to not elide).
    bool linesPresent = true;
    for (int id : ticket) {
        const std::optional<models::Order> o = ctx.orders().order(id);
        if (!o || o->itemCount() == 0) { linesPresent = false; continue; }
        for (const models::OrderItem& li : o->items())
            if (li.name().trimmed().isEmpty()) linesPresent = false;
    }
    check(QStringLiteral("every ticket carries at least one NAMED dish line"), linesPresent);

    checkEq(QStringLiteral("the three columns together equal the kitchen's outstanding workload"),
            boardRows,
            scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM orders WHERE status IN "
                                          "('PENDING','PREPARING','READY')"), {}));
    checkEq(QStringLiteral("...which is exactly the figure the dashboard quotes"),
            ctx.reports().pendingOrderCount(), boardRows);

    // activeOrders() drives the POS's own list; it must be a superset of the kitchen board,
    // never a competing view of it.
    std::set<int> active;
    for (const models::Order& o : ctx.orders().activeOrders()) active.insert(o.id());
    bool boardInsideActive = true;
    for (models::OrderStatus s : board)
        for (int id : idsIn(s))
            if (!active.count(id)) boardInsideActive = false;
    check(QStringLiteral("every ticket on the board also appears in activeOrders()"),
          boardInsideActive);

    // ---- the pass itself ---------------------------------------------------
    {
        const std::vector<int> pass = ctx.orders().kitchenQueue().snapshot();
        const std::set<int> onPass(pass.begin(), pass.end());
        check(QStringLiteral("the ticket still waiting is on the pass"), onPass.count(ticket[0]) == 1);
        check(QStringLiteral("...and the two tickets the chef picked up have LEFT the pass"),
              onPass.count(ticket[1]) == 0 && onPass.count(ticket[2]) == 0);
        check(QStringLiteral("snapshot() is non-destructive — the pass still holds its tickets"),
              ctx.orders().kitchenQueue().size() == pass.size());
    }

    ctx.orders().rebuildQueue();   // what a restart does
    {
        const std::vector<int> pass = ctx.orders().kitchenQueue().snapshot();
        const std::set<int> onPass(pass.begin(), pass.end());
        check(QStringLiteral("a rebuilt pass holds EXACTLY the Pending orders (a restart loses "
                             "nothing and invents nothing)"),
              onPass == pending,
              QStringLiteral("pass %1 ids, Pending %2 ids").arg(onPass.size()).arg(pending.size()));

        // The kitchen works oldest first. Ties are allowed (two tickets can share a second);
        // going backwards is not.
        bool fifo = true;
        QDateTime previous;
        for (int id : pass) {
            const std::optional<models::Order> o = ctx.orders().order(id);
            if (!o) continue;
            if (previous.isValid() && o->createdAt() < previous) fifo = false;
            previous = o->createdAt();
        }
        check(QStringLiteral("...in oldest-first order"), fifo);
    }
}

// =============================================================================
// 11. Per-role payroll — the method-overriding demonstration the GUI surfaces
// =============================================================================

void testRolePayroll(services::AppContext& ctx) {
    section(QStringLiteral("PAYROLL (per-role monthlyPay overrides)"));

    // Identical base salary for all four, so every difference below is the override and
    // nothing else.
    const Money base = Money::fromRupees(40000);

    models::Waiter waiter(0, QStringLiteral("Payroll Waiter"), QStringLiteral("+977-9800222001"),
                          QStringLiteral("pay.waiter@aluchop.com.np"), base,
                          QStringLiteral("DAY"));
    models::Chef chef(0, QStringLiteral("Payroll Chef"), QStringLiteral("+977-9800222002"),
                      QStringLiteral("pay.chef@aluchop.com.np"), base, QStringLiteral("DAY"));
    models::Manager manager(0, QStringLiteral("Payroll Manager"),
                            QStringLiteral("+977-9800222003"),
                            QStringLiteral("pay.manager@aluchop.com.np"), base,
                            QStringLiteral("DAY"));
    models::Admin admin(0, QStringLiteral("Payroll Admin"), QStringLiteral("+977-9800222004"),
                        QStringLiteral("pay.admin@aluchop.com.np"), base, QStringLiteral("DAY"));
    models::Employee plain(0, QStringLiteral("Payroll Base"), QStringLiteral("+977-9800222005"),
                           QStringLiteral("pay.base@aluchop.com.np"), QStringLiteral("WAITER"),
                           base, QStringLiteral("DAY"));

    // Before any extra is declared, every role is honestly paid its base — the overrides ADD,
    // they do not fabricate.
    checkEq(QStringLiteral("with no extras declared, a Waiter is paid exactly base"),
            waiter.monthlyPay().paisa(), base.paisa());
    checkEq(QStringLiteral("...a Chef too"), chef.monthlyPay().paisa(), base.paisa());
    checkEq(QStringLiteral("...a Manager too"), manager.monthlyPay().paisa(), base.paisa());
    checkEq(QStringLiteral("...an Admin too"), admin.monthlyPay().paisa(), base.paisa());
    checkEq(QStringLiteral("...and the un-specialised Employee base rule adds nothing at all"),
            plain.monthlyPay().paisa(), base.paisa());

    // Each role's own component, each a genuinely different rule.
    const Money tips = Money::fromRupees(3500);
    const int overtimeHours = 7;
    const Money bonus = Money::fromRupees(9000);
    const Money adminBonus = Money::fromRupees(15000);

    waiter.addTip(tips);
    chef.setOvertimeHours(overtimeHours);
    manager.setMonthlyBonus(bonus);
    admin.setMonthlyBonus(adminBonus);

    const Money chefExtra =
        models::Chef::kOvertimeRatePerHour * static_cast<std::int64_t>(overtimeHours);

    checkEq(QStringLiteral("a Waiter's override adds TIPS: pay == base + tips"),
            waiter.monthlyPay().paisa(), base.paisa() + tips.paisa());
    checkEq(QStringLiteral("a Chef's override adds OVERTIME: pay == base + rate x hours"),
            chef.monthlyPay().paisa(), base.paisa() + chefExtra.paisa());
    checkEq(QStringLiteral("a Manager's override adds a BONUS: pay == base + bonus"),
            manager.monthlyPay().paisa(), base.paisa() + bonus.paisa());
    checkEq(QStringLiteral("an Admin INHERITS the Manager rule rather than copying it"),
            admin.monthlyPay().paisa(), base.paisa() + adminBonus.paisa());

    // The extra each role defines is genuinely non-zero — the column the GUI labels "Extras"
    // has something to show.
    check(QStringLiteral("the Waiter's extra component is non-zero"),
          (waiter.monthlyPay() - base).paisa() > 0, (waiter.monthlyPay() - base).toString());
    check(QStringLiteral("the Chef's extra component is non-zero"),
          (chef.monthlyPay() - base).paisa() > 0, (chef.monthlyPay() - base).toString());
    check(QStringLiteral("the Manager's extra component is non-zero"),
          (manager.monthlyPay() - base).paisa() > 0, (manager.monthlyPay() - base).toString());
    check(QStringLiteral("the Admin's extra component is non-zero"),
          (admin.monthlyPay() - base).paisa() > 0, (admin.monthlyPay() - base).toString());

    // On an IDENTICAL base salary the four take home four different amounts. This is the
    // assertion that fails the moment somebody "simplifies" the hierarchy into one formula.
    {
        const std::array<long long, 4> pay{waiter.monthlyPay().paisa(), chef.monthlyPay().paisa(),
                                           manager.monthlyPay().paisa(),
                                           admin.monthlyPay().paisa()};
        bool allDifferent = true;
        for (std::size_t i = 0; i < pay.size(); ++i)
            for (std::size_t j = i + 1; j < pay.size(); ++j)
                if (pay[i] == pay[j]) allDifferent = false;
        check(QStringLiteral("on the SAME base salary, Waiter / Chef / Manager / Admin all take "
                             "home DIFFERENT amounts"),
              allDifferent,
              QStringLiteral("%1 / %2 / %3 / %4").arg(pay[0]).arg(pay[1]).arg(pay[2]).arg(pay[3]));
        bool allAboveBase = true;
        for (long long p : pay) if (p <= base.paisa()) allAboveBase = false;
        check(QStringLiteral("...and every one of them exceeds the shared base"), allAboveBase);
    }

    // Runtime polymorphism: the payroll loop holds Employee* and never asks what it points at.
    {
        std::vector<std::unique_ptr<models::Employee>> roster;
        roster.push_back(std::make_unique<models::Waiter>(waiter));
        roster.push_back(std::make_unique<models::Chef>(chef));
        roster.push_back(std::make_unique<models::Manager>(manager));
        roster.push_back(std::make_unique<models::Admin>(admin));

        const std::array<long long, 4> expected{
            base.paisa() + tips.paisa(), base.paisa() + chefExtra.paisa(),
            base.paisa() + bonus.paisa(), base.paisa() + adminBonus.paisa()};
        const std::array<QString, 4> expectedRole{
            QStringLiteral("Waiter"), QStringLiteral("Chef"), QStringLiteral("Manager"),
            QStringLiteral("Admin")};

        bool dispatched = true;
        bool named = true;
        QString drift;
        for (std::size_t i = 0; i < roster.size(); ++i) {
            const models::Employee* through = roster[i].get();   // base pointer, nothing else
            if (through->monthlyPay().paisa() != expected[i]) {
                dispatched = false;
                if (drift.isEmpty())
                    drift = QStringLiteral("%1 paid %2 through Employee*, expected %3")
                                .arg(expectedRole[i])
                                .arg(through->monthlyPay().paisa())
                                .arg(expected[i]);
            }
            if (through->roleName() != expectedRole[i]) {
                named = false;
                if (drift.isEmpty())
                    drift = QStringLiteral("roleName() through Employee* said '%1', expected '%2'")
                                .arg(through->roleName(), expectedRole[i]);
            }
        }
        check(QStringLiteral("monthlyPay() through a base Employee* lands on the ROLE's rule, "
                             "not the base one"),
              dispatched, drift);
        check(QStringLiteral("...and roleName() names the concrete role through the same pointer"),
              named, drift);
    }

    // The extras are guarded, not free-form.
    checkThrows<aluchop::core::ValidationException>(
        QStringLiteral("a negative tip is refused"),
        [&waiter] { waiter.addTip(Money::fromRupees(-1)); });
    checkThrows<aluchop::core::ValidationException>(
        QStringLiteral("negative overtime is refused"),
        [&chef] { chef.setOvertimeHours(-1); });
    checkThrows<aluchop::core::ValidationException>(
        QStringLiteral("a negative bonus is refused"),
        [&manager] { manager.setMonthlyBonus(Money::fromRupees(-1)); });

    // ---- the service path the screen actually reads -------------------------
    const Money hiredSalary = Money::fromRupees(52000);
    const std::array<std::pair<QString, QString>, 4> hires{{
        {QStringLiteral("WAITER"), QStringLiteral("+977-9800333001")},
        {QStringLiteral("CHEF"), QStringLiteral("+977-9800333002")},
        {QStringLiteral("MANAGER"), QStringLiteral("+977-9800333003")},
        {QStringLiteral("ADMIN"), QStringLiteral("+977-9800333004")},
    }};
    std::map<int, QString> hiredRole;
    for (const auto& h : hires) {
        auto hired = ctx.employees().hire(
            QStringLiteral("Payroll %1").arg(h.first), h.second,
            QStringLiteral("payroll.%1@aluchop.com.np").arg(h.first.toLower()), h.first,
            hiredSalary, QStringLiteral("DAY"));
        if (hired.isOk()) hiredRole[hired.value()] = h.first;
        check(QStringLiteral("a new %1 can be hired").arg(h.first.toLower()), hired.isOk(),
              hired.isErr() ? hired.error() : QString());
    }

    // The repository's factory builds the CONCRETE class from the position column — otherwise
    // every override above is unreachable in the running application.
    {
        const std::vector<std::unique_ptr<models::Employee>> staff = ctx.employees().staff();
        std::map<int, const models::Employee*> byId;
        for (const auto& person : staff) if (person) byId[person->id()] = person.get();

        bool builtRight = true;
        QString wrongClass;
        for (const auto& hire : hiredRole) {
            const auto it = byId.find(hire.first);
            if (it == byId.end()) { builtRight = false; continue; }
            if (it->second->roleName().toUpper() != hire.second) {
                builtRight = false;
                if (wrongClass.isEmpty())
                    wrongClass = QStringLiteral("position %1 came back as a %2")
                                     .arg(hire.second, it->second->roleName());
            }
        }
        check(QStringLiteral("the repository builds the CONCRETE role class from the position "
                             "column"),
              builtRight, wrongClass);

        // The payroll table and the payroll headline are two widgets fed by two calls. They must
        // never disagree about one person's pay.
        const auto preview = ctx.employees().payrollPreview();
        std::map<QString, long long> previewByLabel;
        for (const auto& row : preview)
            previewByLabel[std::get<0>(row)] = std::get<2>(row).paisa();

        bool agree = true;
        QString mismatch;
        long long counted = 0;
        for (const auto& person : staff) {
            if (!person || !person->isActive()) continue;
            ++counted;
            const auto it = previewByLabel.find(person->displayLabel());
            if (it == previewByLabel.end()) {
                agree = false;
                if (mismatch.isEmpty())
                    mismatch = QStringLiteral("%1 is missing from payrollPreview()")
                                   .arg(person->displayLabel());
                continue;
            }
            if (it->second != person->monthlyPay().paisa()) {
                agree = false;
                if (mismatch.isEmpty())
                    mismatch = QStringLiteral("%1: preview %2, monthlyPay() %3")
                                   .arg(person->displayLabel())
                                   .arg(it->second)
                                   .arg(person->monthlyPay().paisa());
            }
        }
        check(QStringLiteral("payrollPreview() quotes the SAME pay the payroll table computes "
                             "from staff() — the two figures cannot contradict each other"),
              agree, mismatch);
        checkEq(QStringLiteral("...for every ACTIVE employee and no leaver"),
                static_cast<long long>(preview.size()), counted);

        bool descending = true;
        for (std::size_t i = 1; i < preview.size(); ++i)
            if (std::get<2>(preview[i - 1]) < std::get<2>(preview[i])) descending = false;
        check(QStringLiteral("...highest earner first, the order a payroll sheet is checked in"),
              descending);

        // Extras are per-MONTH figures the employees table does not carry, so a row read back
        // out of the database is paid its base and nothing more. Anything else would be a
        // number the application invented.
        bool noInvention = true;
        QString invented;
        for (const auto& hire : hiredRole) {
            const auto it = byId.find(hire.first);
            if (it == byId.end()) continue;
            if (it->second->monthlyPay().paisa() != hiredSalary.paisa()) {
                noInvention = false;
                if (invented.isEmpty())
                    invented = QStringLiteral("%1 read back as %2, salary was %3")
                                   .arg(hire.second)
                                   .arg(it->second->monthlyPay().paisa())
                                   .arg(hiredSalary.paisa());
            }
        }
        check(QStringLiteral("a role read back from the database is paid its stored base — no "
                             "extra is invented for a month nothing was recorded in"),
              noInvention, invented);
    }
}

// =============================================================================
// 12. Customer visit counts — one number, every screen
// =============================================================================

void testCustomerVisitCounts(services::AppContext& ctx, Fixture& fx) {
    section(QStringLiteral("CUSTOMER VISIT COUNTS (directory vs profile)"));

    auto created = ctx.customers().create(QStringLiteral("Visit Count Guest"),
                                          QStringLiteral("+977-9800444001"),
                                          QStringLiteral("visits@aluchop.com.np"));
    check(QStringLiteral("a guest can be registered for the visit-count check"), created.isOk(),
          created.isErr() ? created.error() : QString());
    if (created.isErr()) return;
    const int guestId = created.value();

    /// The number the DIRECTORY column shows: the customer as it appears in the full listing.
    const auto directoryVisits = [&ctx, guestId]() -> int {
        for (const models::Customer& c : ctx.customers().all())
            if (c.id() == guestId) return c.visits();
        return -1;
    };
    /// The number the PROFILE panel shows: the customer loaded on its own.
    const auto profileVisits = [&ctx, guestId]() -> int {
        const std::optional<models::Customer> c = ctx.customers().byId(guestId);
        return c ? c->visits() : -1;
    };
    /// The number the SEARCH results show.
    const auto searchVisits = [&ctx, guestId]() -> int {
        for (const models::Customer& c : ctx.customers().search(QStringLiteral("Visit Count")))
            if (c.id() == guestId) return c.visits();
        return -1;
    };

    checkEq(QStringLiteral("a brand-new guest reads zero visits in the directory"),
            directoryVisits(), 0);
    checkEq(QStringLiteral("...and zero in the profile — the same number, not a coincidence"),
            profileVisits(), directoryVisits());
    checkEq(QStringLiteral("...and zero in the search results"), searchVisits(),
            directoryVisits());

    // One served order == one visit.
    auto first = ctx.orders().createOrder(models::OrderType::DineIn, fx.tableB, guestId, 0);
    check(QStringLiteral("an order can be raised against the guest"), first.isOk(),
          first.isErr() ? first.error() : QString());
    if (first.isErr()) return;
    const int firstId = first.value().id();
    ctx.orders().addItem(firstId, fx.dishes[0].id(), 2);

    QString why;
    check(QStringLiteral("the order is served"), serveOrder(ctx, firstId, why), why);

    checkEq(QStringLiteral("serving the order records exactly ONE visit"), profileVisits(), 1);
    checkEq(QStringLiteral("THE DIRECTORY AND THE PROFILE AGREE after the visit"),
            directoryVisits(), profileVisits());
    checkEq(QStringLiteral("...and so does the search result for the same guest"),
            searchVisits(), profileVisits());

    // The history panel's caption reads "<itemised> of <visits> itemised". Those two numbers are
    // only a sentence if the itemised list is a SUBSET of the recorded visits.
    checkEq(QStringLiteral("the visit history lists the one served order"),
            static_cast<long long>(ctx.customers().visitHistory(guestId).size()), 1);
    check(QStringLiteral("the itemised count does not exceed the visit counter "
                         "(the profile caption's 'N of M' is a subset, not two rival tallies)"),
          static_cast<int>(ctx.customers().visitHistory(guestId).size()) <= profileVisits(),
          QStringLiteral("%1 itemised vs %2 visits")
              .arg(ctx.customers().visitHistory(guestId).size())
              .arg(profileVisits()));

    // Settling the bill awards LOYALTY POINTS; it must not award a second visit.
    auto bill = ctx.billing().prepareBill(firstId);
    if (bill.isOk()) {
        const Money total = bill.value().total();
        auto settled = ctx.billing().settle(firstId, bill.value(), models::PaymentMethod::Cash,
                                            total, 1);
        check(QStringLiteral("the guest's bill settles"), settled.isOk(),
              settled.isErr() ? settled.error() : QString());
        if (settled.isOk()) {
            fx.expectedRevenuePaisa += total.paisa();
            tallyOrder(ctx, firstId, fx.soldQty);
        }
    }
    checkEq(QStringLiteral("settling the bill does NOT double-count the visit"), profileVisits(), 1);
    checkEq(QStringLiteral("...and the directory still agrees"), directoryVisits(),
            profileVisits());

    // Loyalty points are the other number both screens print; they must reconcile too.
    {
        const std::optional<models::Customer> profile = ctx.customers().byId(guestId);
        int listed = -1;
        for (const models::Customer& c : ctx.customers().all())
            if (c.id() == guestId) listed = c.loyaltyPoints();
        checkEq(QStringLiteral("the loyalty balance also reads the same in both views"),
                listed, profile ? profile->loyaltyPoints() : -2);
    }

    // A second served order moves BOTH numbers by exactly one, together.
    const int before = profileVisits();
    auto second = ctx.orders().createOrder(models::OrderType::Takeaway, 0, guestId, 0);
    if (second.isOk()) {
        const int secondId = second.value().id();
        ctx.orders().addItem(secondId, fx.dishes[1].id(), 1);
        QString why2;
        check(QStringLiteral("a second order is served for the same guest"),
              serveOrder(ctx, secondId, why2), why2);
        checkEq(QStringLiteral("the visit counter advances by exactly one"), profileVisits(),
                before + 1);
        checkEq(QStringLiteral("...in the directory too, in lockstep"), directoryVisits(),
                profileVisits());
        checkEq(QStringLiteral("...and the itemised history grows with it"),
                static_cast<long long>(ctx.customers().visitHistory(guestId).size()),
                profileVisits());
    }

    // The stored counter is the one the SQL row carries: no view recomputes it privately.
    checkEq(QStringLiteral("both views quote the visits column straight out of the database"),
            profileVisits(),
            scalarLongLong(QStringLiteral("SELECT visits FROM customers WHERE id = ?"),
                           {guestId}));
}

// =============================================================================
// 13. Report tallies — the row count and the window must describe one data set
// =============================================================================

void testReportTallies(services::AppContext& ctx) {
    section(QStringLiteral("REPORT TALLIES (rows vs the window they describe)"));

    /// The GUI's rule: a row whose first cell reads "TOTAL" is a summary line, not a subject line.
    const auto isSummary = [](const QStringList& row) {
        return !row.isEmpty()
               && row.at(0).trimmed().compare(QLatin1String("TOTAL"), Qt::CaseInsensitive) == 0;
    };

    const QDate today = QDate::currentDate();

    struct Window { int days; };
    const std::array<Window, 3> windows{{{1}, {7}, {30}}};

    for (const Window& w : windows) {
        const QDate from = today.addDays(-(w.days - 1));
        auto report = ctx.reports().makeReport(services::ReportKind::Sales, from, today);
        if (!report) {
            check(QStringLiteral("a %1-day Sales report can be generated").arg(w.days), false);
            continue;
        }
        const std::vector<QStringList> rows = report->rows();

        long long summary = 0;
        for (const QStringList& r : rows) if (isSummary(r)) ++summary;
        const long long total = static_cast<long long>(rows.size());
        const long long data = total - summary;

        checkEq(QStringLiteral("a %1-day Sales window yields %1 subject rows — the row count and "
                               "the window describe the SAME data set").arg(w.days),
                data, w.days);
        checkEq(QStringLiteral("...plus exactly one grand-total line (%1-day window)").arg(w.days),
                summary, 1);
        checkEq(QStringLiteral("...so the %1-day badge's total is data + summary, with nothing "
                               "left over").arg(w.days),
                total, data + summary);

        bool widthOk = true;
        for (const QStringList& r : rows)
            if (r.size() != report->header().size()) widthOk = false;
        check(QStringLiteral("...and every row is as wide as the header (%1 columns)")
                  .arg(report->header().size()),
              widthOk);

        // The subject rows are the window's days: distinct, consecutive, and inside the range.
        bool datesOk = true;
        QString dateProblem;
        QDate expected = from;
        for (const QStringList& r : rows) {
            if (isSummary(r)) continue;
            const QDate cell = QDate::fromString(r.at(0), Qt::ISODate);
            if (cell != expected) {
                datesOk = false;
                if (dateProblem.isEmpty())
                    dateProblem = QStringLiteral("row read '%1', the window's next day is %2")
                                      .arg(r.at(0), expected.toString(Qt::ISODate));
            }
            expected = expected.addDays(1);
        }
        check(QStringLiteral("...and those %1 rows ARE the %1 consecutive days of the window")
                  .arg(w.days),
              datesOk, dateProblem);

        // The grand total is the sum of the very rows printed above it — not a second query.
        long long rowRevenue = 0;
        long long rowOrders = 0;
        long long grandRevenue = -1;
        long long grandOrders = -1;
        bool parsed = true;
        for (const QStringList& r : rows) {
            long long money = 0;
            if (!parseNpr(r.at(2), money)) { parsed = false; continue; }
            if (isSummary(r)) {
                grandRevenue = money;
                grandOrders = r.at(1).toLongLong();
            } else {
                rowRevenue += money;
                rowOrders += r.at(1).toLongLong();
            }
        }
        check(QStringLiteral("every money cell in the %1-day report parses as NPR").arg(w.days),
              parsed);
        checkEq(QStringLiteral("the %1-day grand total is the sum of its own rows").arg(w.days),
                grandRevenue, rowRevenue);
        checkEq(QStringLiteral("...and so is the %1-day report's order count").arg(w.days),
                grandOrders, rowOrders);

        // The dashboard and the exported report must not describe two different days.
        long long dashboard = 0;
        for (int i = 0; i < w.days; ++i)
            dashboard += ctx.reports().salesForDay(from.addDays(i)).paisa();
        checkEq(QStringLiteral("the %1-day report's revenue equals the dashboard's own "
                               "salesForDay() over the identical days").arg(w.days),
                rowRevenue, dashboard);
    }

    // An inverted range describes nothing, and must therefore print nothing.
    {
        auto inverted =
            ctx.reports().makeReport(services::ReportKind::Sales, today, today.addDays(-3));
        check(QStringLiteral("an inverted date range yields NO rows, not a silent full dump"),
              inverted && inverted->rows().empty(),
              inverted ? QStringLiteral("%1 rows").arg(inverted->rows().size())
                       : QStringLiteral("no report"));
    }

    // ---- Orders: the row count is the number of orders in the window --------
    {
        const QDate from = today.addDays(-2);
        const QDate to = today.addDays(2);
        auto report = ctx.reports().makeReport(services::ReportKind::Orders, from, to);
        check(QStringLiteral("a five-day Orders report can be generated"), report != nullptr);
        if (report) {
            const std::vector<QStringList> rows = report->rows();
            // Every order in this throwaway database was raised minutes ago, so a window that
            // straddles today by two days on each side must contain all of them.
            checkEq(QStringLiteral("the Orders report's row count equals the number of orders in "
                                   "the window it names"),
                    static_cast<long long>(rows.size()),
                    scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM orders"), {}));

            bool identified = true;
            bool inWindow = true;
            QString problem;
            for (const QStringList& r : rows) {
                if (r.isEmpty() || r.at(0).trimmed().isEmpty()) {
                    identified = false;
                    if (problem.isEmpty()) problem = QStringLiteral("a row has no order number");
                    continue;
                }
                const QDateTime when =
                    QDateTime::fromString(r.at(5), QStringLiteral("yyyy-MM-dd HH:mm"));
                if (!when.isValid() || when.date() < from || when.date() > to) {
                    inWindow = false;
                    if (problem.isEmpty())
                        problem = QStringLiteral("row '%1' is dated '%2', outside %3..%4")
                                      .arg(r.at(0), r.at(5), from.toString(Qt::ISODate),
                                           to.toString(Qt::ISODate));
                }
            }
            check(QStringLiteral("every Orders row carries its order number — the one column a "
                                 "listing may never drop"),
                  identified, problem);
            check(QStringLiteral("...and every row is dated inside the window it was asked for"),
                  inWindow, problem);
        }
    }
}

// =============================================================================
// Suite runner
// =============================================================================

void runSuites(services::AppContext& ctx, Fixture& fx) {
    testAuth(ctx);
    testOrderLifecycle(ctx, fx);
    testMoney(ctx, fx);
    testEmptyText(ctx, fx);
    testInventory(ctx, fx);
    testSplitMerge(ctx, fx);
    testReservations(ctx, fx);
    testFileHandling(ctx, fx);
    testAuditMirror(ctx);
    testRoundTrip(ctx, fx);
    testReports(ctx, fx);

    // The four suites below guard the service contracts the polished GUI reads. They run LAST
    // because they raise fresh orders, hire staff and register a guest; running them earlier
    // would move the very totals the suites above hand-count.
    testKitchenPass(ctx, fx);
    testRolePayroll(ctx);
    testCustomerVisitCounts(ctx, fx);
    testReportTallies(ctx);
}

/// Rebuilds the parts of the fixture that are read out of the database.
void loadFixture(services::AppContext& ctx, Fixture& fx) {
    fx.dishes.clear();
    for (const models::MenuItem& mi : ctx.menu().all()) {
        if (!mi.isAvailable()) continue;
        if (ctx.menu().recipeFor(mi.id()).empty()) continue;
        fx.dishes.push_back(mi);
        if (fx.dishes.size() >= 4) break;
    }
    fx.tableA = 0;
    fx.tableB = 0;
    for (const models::Table& t : ctx.reservations().tables()) {
        if (!t.isActive()) continue;
        if (fx.tableA == 0) { fx.tableA = t.id(); continue; }
        if (fx.tableB == 0) { fx.tableB = t.id(); break; }
    }
}

} // namespace

// =============================================================================
// main
// =============================================================================

int main(int argc, char** argv) {
    // Headless: the PDF exporter and the linked GUI translation units need a QApplication,
    // but no window is ever shown.
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    out(QStringLiteral("AluChop — headless end-to-end business-logic assertions"));

    // ---- throwaway data directory ------------------------------------------
    const QString dataDir =
        QDir(QDir::tempPath())
            .filePath(QStringLiteral("aluchop_e2e_%1_%2")
                          .arg(QDateTime::currentMSecsSinceEpoch())
                          .arg(QCoreApplication::applicationPid()));
    if (!QDir().mkpath(dataDir)) {
        std::cerr << "could not create the throwaway data dir" << std::endl;
        return 2;
    }
    out(QStringLiteral("throwaway data dir: %1").arg(dataDir));

    // ---- locate the menu seed ----------------------------------------------
    QString seed;
    const QStringList candidates = {
        QDir::current().filePath(QStringLiteral("assets/menu/menu_seed.json")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("assets/menu/menu_seed.json")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../assets/menu/menu_seed.json")),
        QStringLiteral("/Users/samriddhagc/LocalProjects/AluChop/assets/menu/menu_seed.json"),
    };
    for (const QString& c : candidates)
        if (QFileInfo::exists(c)) { seed = c; break; }
    if (seed.isEmpty()) {
        std::cerr << "could not find assets/menu/menu_seed.json" << std::endl;
        return 2;
    }
    out(QStringLiteral("menu seed:          %1").arg(seed));

    int exitCode = 1;
    try {
        services::AppContext ctx(dataDir, seed);

        Fixture fx;
        fx.dataDir = dataDir;

        section(QStringLiteral("FIXTURE"));
        check(QStringLiteral("the throwaway database seeded the menu"),
              scalarLongLong(QStringLiteral("SELECT COUNT(*) FROM menu_items"), {}) > 100);
        check(QStringLiteral("the throwaway database is NOT the user's real database"),
              !persistence::Database::instance().filePath().contains(
                  QStringLiteral("Application Support")),
              persistence::Database::instance().filePath());

        loadFixture(ctx, fx);
        check(QStringLiteral("at least four available dishes with recipes are seeded"),
              fx.dishes.size() >= 4,
              QStringLiteral("%1 found").arg(fx.dishes.size()));
        check(QStringLiteral("at least two active tables are seeded"),
              fx.tableA != 0 && fx.tableB != 0);

        if (fx.dishes.size() < 4 || fx.tableA == 0 || fx.tableB == 0) {
            out(QStringLiteral("fixture incomplete — the suites cannot run honestly"));
        } else {
            // ONE pass, against the schema exactly as the application ships it.
            runSuites(ctx, fx);
        }
    } catch (const aluchop::core::AluChopException& ex) {
        out(QStringLiteral("FATAL (%1): %2")
                .arg(QString::fromUtf8(ex.category()), QString::fromUtf8(ex.what())));
        ++g_total;
        ++g_failed;
        g_failures << QStringLiteral("the suite aborted: %1").arg(QString::fromUtf8(ex.what()));
    } catch (const std::exception& ex) {
        out(QStringLiteral("FATAL: %1").arg(QString::fromUtf8(ex.what())));
        ++g_total;
        ++g_failed;
        g_failures << QStringLiteral("the suite aborted: %1").arg(QString::fromUtf8(ex.what()));
    }

    // ---- summary ------------------------------------------------------------
    out(QString());
    out(QString(72, QLatin1Char('=')));
    out(QStringLiteral("SUMMARY   assertions: %1   passed: %2   failed: %3")
            .arg(g_total).arg(g_passed).arg(g_failed));
    out(QString(72, QLatin1Char('=')));
    if (!g_failures.isEmpty()) {
        out(QStringLiteral("FAILURES:"));
        for (int i = 0; i < g_failures.size(); ++i)
            out(QStringLiteral("  %1) %2").arg(i + 1).arg(g_failures.at(i)));
    }

    exitCode = g_failed == 0 ? 0 : 1;

    // Leave the throwaway directory behind on failure so the evidence can be inspected.
    if (exitCode == 0)
        QDir(dataDir).removeRecursively();
    else
        out(QStringLiteral("evidence left in: %1").arg(dataDir));

    return exitCode;
}

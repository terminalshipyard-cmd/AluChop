# AluChop — Frozen Architecture Contract

> **Status: BINDING.** Downstream agents implement exactly what is written here — same file names,
> same class names, same signatures. Any deviation is a defect. Read `SPEC.md` and `TOOLCHAIN.md`
> first; this document never contradicts them.
>
> Global conventions (apply everywhere, not repeated per class):
> - Every header starts with `#pragma once`.
> - Every declaration lives in its stated namespace (C++17 nested form, e.g. `namespace aluchop::models {`).
> - Money is `aluchop::core::Money` (integer paisa). **No `double` ever holds currency.**
> - Prices are tax-inclusive. No code path adds tax.
> - Dates/times cross layers as `QDate` / `QDateTime` (stored in SQLite as ISO-8601 TEXT, UTC).
> - `/// @oop-concept <Concept> :: <justification>` tags are mandatory where this document places them.

---

## 1. Layer map

Five namespaces, strict one-way dependency, top depends on bottom only:

```
aluchop::gui          (Qt Widgets / Charts / Svg / PrintSupport)   — presentation only, ZERO SQL
   │ may include: services, models, core
aluchop::services     (QtCore only)                                 — business rules, orchestration
   │ may include: persistence, models, core
aluchop::persistence  (QtCore + QtSql + <fstream>)                  — SQLite repos, raw file layer
   │ may include: models, core
aluchop::models       (QtCore only)                                 — domain entities, no I/O
   │ may include: core
aluchop::core         (QtCore + std only)                           — Money, Result, exceptions, logger
```

Qt-module allowance per layer (a compile-time greppable rule):

| Layer | Allowed Qt includes | Forbidden |
|---|---|---|
| core | QtCore (`QString`, `QDate`, `QDateTime`) | everything else |
| models | QtCore + `QJsonObject` | QtSql, QtWidgets |
| persistence | QtCore, QtSql | QtWidgets, QtGui |
| services | QtCore (incl. `QObject` for `NotificationService`); persistence headers | **writing SQL.** No `QSqlQuery`/`QSqlDatabase` use and no SQL string may appear in any `services` file — SQL lives only in persistence |
| gui | all verified modules: Widgets, Charts, Svg, PrintSupport, Gui, Core | **naming any `aluchop::persistence` type, including any `persistence/` header** (this is what makes "GUI never contains SQL" mechanically checkable); QML; WebEngine |

> **Corrected at contract-seal (was: "no QtSql headers in public services headers").** That rule was
> unachievable and is now retired: `AppContext` owns every repository **by value**, so `AppContext.hpp`
> must see their complete types, and every `gui` `.cpp` includes `AppContext.hpp`. Forbidding QtSql in
> services headers therefore could never have stopped QtSql declarations reaching a GUI translation
> unit, and enforcing it elsewhere would have bought nothing. The two rules above are the ones that
> actually carry the intent and can be grepped:
>
> ```bash
> # must both print nothing
> grep -rnE 'QSqlQuery|QSqlDatabase|QSqlRecord|SELECT |INSERT |UPDATE |DELETE ' src/services include/aluchop/services
> grep -rnE 'persistence::|aluchop/persistence/' src/gui include/aluchop/gui src/main.cpp
> ```
>
> Both are clean across the sealed header tree. The second is the reason
> `AuditService` grew `verifyTrailIntegrity()` and friends (§3.4, reconciliation R1).

Only classes that need signals/slots carry `Q_OBJECT`: every `gui` widget class and
`services::NotificationService`. Nothing else.

---

## 2. Complete file manifest

This manifest is **binding**: downstream agents create exactly these files, no others.
`(h-only)` = header-only, no `.cpp`.

### LANE-CORE — `include/aluchop/core/`, `src/core/`

| File | Purpose |
|---|---|
| `include/aluchop/core/Money.hpp` + `src/core/Money.cpp` | NPR value type in integer paisa; full operator suite; `toString()` formatting |
| `include/aluchop/core/Result.hpp` (h-only) | `Result<T>` class template + `Result<void>` specialisation — value-or-error return |
| `include/aluchop/core/Exceptions.hpp` (h-only) | `AluChopException` base + Database/Validation/Auth/Inventory/FileIO exception hierarchy |
| `include/aluchop/core/Logger.hpp` + `src/core/Logger.cpp` | append-mode `<fstream>` text logger, Meyers singleton, static message counter |
| `include/aluchop/core/Algorithms.hpp` (h-only) | function templates `sumMoney`, `countMatching`, `clampValue` |
| `include/aluchop/core/AppInfo.hpp` (h-only) | const `AppInfo` object (credits) + const array of the 14 menu categories |

### LANE-MODELS — `include/aluchop/models/`, `src/models/`

| File | Purpose |
|---|---|
| `Enums.hpp` + `src/models/Enums.cpp` | `OrderType`, `OrderStatus`, `PaymentMethod`, `PromoKind`, `ReservationStatus`, `UserRole`, `NoticeLevel` + to/from-string |
| `Interfaces.hpp` (h-only) | pure-abstract mixins `IPrintable`, `ISerializable`, `IAuditable`, `IDiscountable` |
| `Person.hpp` + `Person.cpp` | abstract base of the people diamond; validated identity fields |
| `Employee.hpp` + `Employee.cpp` | `virtual public Person`; salary/shift/position; virtual `monthlyPay()` |
| `Customer.hpp` + `Customer.cpp` | `virtual public Person`; loyalty points, visits, `operator++` |
| `Waiter.hpp` + `Waiter.cpp` | `public Employee`; tips, tables served; overrides `monthlyPay()` |
| `Chef.hpp` + `Chef.cpp` | `public Employee`; specialty, overtime; overrides `monthlyPay()` |
| `Manager.hpp` + `Manager.cpp` | `public Employee`; bonus; overrides `monthlyPay()` |
| `Admin.hpp` + `Admin.cpp` | `public Manager, public IAuditable` — multilevel + multiple |
| `StaffCustomer.hpp` + `StaffCustomer.cpp` | `public Employee, public Customer` — the diamond; staff discount |
| `User.hpp` + `User.cpp` | login account: username, salted hash, role, remember token, security question |
| `MenuItem.hpp` + `MenuItem.cpp` | menu entry; implements `ISerializable`; `==` and `<` |
| `OrderItem.hpp` + `OrderItem.cpp` | line item snapshot (name + unit price frozen at order time) |
| `Order.hpp` + `Order.cpp` | order aggregate; copy ctor/assignment (split), `operator+=` (merge), `operator[]`, static open-order counter |
| `Bill.hpp` + `Bill.cpp` | immutable bill snapshot; `IPrintable` + `IDiscountable`; friend `services::BillingService` settles it |
| `Payment.hpp` + `Payment.cpp` | persisted payment record incl. tendered/change |
| `Ingredient.hpp` + `Ingredient.cpp` | stock item; low-stock and expiry predicates |
| `Supplier.hpp` + `Supplier.cpp` | supplier contact data |
| `RecipeLine.hpp` (h-only) | POD struct: menu item ↔ ingredient quantity |
| `Reservation.hpp` + `Reservation.cpp` | table booking with time window and guests |
| `Table.hpp` + `Table.cpp` | physical table: name, capacity |
| `Promo.hpp` + `Promo.cpp` | promo code: percent/flat, validity window, min order |

### LANE-PERSIST — `include/aluchop/persistence/`, `src/persistence/`

| File | Purpose |
|---|---|
| `Database.hpp` + `Database.cpp` | QSQLITE connection singleton; `exec`/`prepared` helpers; `transaction()` with rollback + **rethrow** |
| `SchemaMigrator.hpp` + `SchemaMigrator.cpp` | versioned DDL migrations + first-run seeding (menu JSON, admin user, tables, ingredients) |
| `Repository.hpp` (h-only) | `Repository<T>` class template: generic `findAll`/`findById`/`count`/`removeById` |
| `UserRepository.hpp` + `.cpp` | users CRUD, lookup by username / remember-token |
| `MenuRepository.hpp` + `.cpp` | menu items CRUD, search/filter, availability; owns `recipes` table access |
| `CustomerRepository.hpp` + `.cpp` | customers CRUD, phone lookup, loyalty/visits update |
| `EmployeeRepository.hpp` + `.cpp` | employees CRUD + `attendance` table access |
| `OrderRepository.hpp` + `.cpp` | order + order_items persistence, status updates, date-range queries |
| `IngredientRepository.hpp` + `.cpp` | ingredients CRUD + `inventory_transactions` log |
| `SupplierRepository.hpp` + `.cpp` | suppliers CRUD |
| `TableRepository.hpp` + `.cpp` | tables CRUD |
| `ReservationRepository.hpp` + `.cpp` | reservations CRUD + overlap queries |
| `PaymentRepository.hpp` + `.cpp` | payments insert + revenue aggregation queries |
| `PromoRepository.hpp` + `.cpp` | promos CRUD, code lookup with validity check |
| `SettingsRepository.hpp` + `.cpp` | key/value settings table |
| `AuditRepository.hpp` + `.cpp` | SQLite mirror of the audit trail (queryable) |
| `BinaryRecordFile.hpp` + `.cpp` | fixed-128-byte-record binary file, `seekg`/`seekp` random access, checksum |
| `AuditTrail.hpp` + `.cpp` | **private** inheritance of `BinaryRecordFile`; sequenced, checksummed audit log |
| `CsvWriter.hpp` + `.cpp` | sequential ASCII CSV writer, escaping, per-write stream error checks |
| `BackupManager.hpp` + `.cpp` | timestamped `.db` copy backup, validated restore (SQLite header magic), export |

### LANE-SERVICES — `include/aluchop/services/`, `src/services/`

| File | Purpose |
|---|---|
| `AppContext.hpp` + `.cpp` | composition root: owns repos + services as value members, wires references |
| `AuthService.hpp` + `.cpp` | login/logout, salted SHA-256, roles, remember-me, forgot-password (security question) |
| `AuditService.hpp` + `.cpp` | single entry point that writes both `AuditTrail` (binary) and `audit_log` (SQLite) |
| `MenuService.hpp` + `.cpp` | menu browse/search/sort/filter, availability toggle, CRUD |
| `OrderService.hpp` + `.cpp` | order lifecycle, split (copy ctor) / merge (`operator+=`), kitchen pipeline, inventory trigger |
| `KitchenQueue.hpp` (h-only) | FIFO of pending order ids over `std::queue` |
| `BillingService.hpp` + `.cpp` | bill computation (tax-inclusive!), promo/staff/manual discount, service charge, settle + change |
| `CustomerService.hpp` + `.cpp` | customer CRUD, visit recording (`++customer`), loyalty, favourites, history |
| `EmployeeService.hpp` + `.cpp` | staff CRUD via polymorphic factory (`unique_ptr<Employee>`), attendance, payroll preview |
| `InventoryService.hpp` + `.cpp` | stock deduction from recipes, restock, low-stock & expiry alerts, supplier CRUD |
| `ReservationService.hpp` + `.cpp` | availability check, booking, seating, cancellation |
| `ReportService.hpp` + `.cpp` | dashboard aggregates + report factory |
| `ReportGenerator.hpp` + `.cpp` | abstract generator (**protected** inheritance of `CsvWriter`) + 5 concrete reports |
| `SettingsService.hpp` + `.cpp` | restaurant info, theme pref, backup/restore/export orchestration |
| `NotificationService.hpp` + `.cpp` | the only `QObject` service: typed signals `notification` + `dataChanged` |
| `Commands.hpp` + `.cpp` | `Command` abstract base, concrete undo/redo commands, `CommandStack` |

### LANE-GUI — `include/aluchop/gui/`, `src/gui/`

| File | Purpose |
|---|---|
| `ThemeManager.hpp` + `.cpp` | Sage-Green light/dark `Palette` structs → runtime-generated QSS; live switch |
| `SplashScreen.hpp` + `.cpp` | fading splash/loading screen |
| `LoginWindow.hpp` + `.cpp` | admin/employee login, remember me, forgot-password flow |
| `MainWindow.hpp` + `.cpp` | shell: sidebar + `QStackedWidget` pages + footer credit + shortcuts + undo/redo |
| `Sidebar.hpp` + `.cpp` | icon navigation rail |
| `Page.hpp` + `.cpp` | abstract page base (`pageTitle()`, `refresh()` pure virtual) |
| `DashboardPage.hpp` + `.cpp` | animated stat cards, QtCharts revenue graph, alerts, pending orders |
| `MenuPage.hpp` + `.cpp` | browse/search/sort/filter menu, availability toggle, item CRUD |
| `OrdersPage.hpp` + `.cpp` | order list + kitchen board + order editor + split/merge |
| `CustomersPage.hpp` + `.cpp` | customer database, loyalty, history, favourites |
| `EmployeesPage.hpp` + `.cpp` | staff table, attendance marking, payroll preview |
| `InventoryPage.hpp` + `.cpp` | ingredients, stock, suppliers, restock, alerts |
| `ReservationsPage.hpp` + `.cpp` | bookings calendar/list, availability, new reservation |
| `ReportsPage.hpp` + `.cpp` | charts + CSV/PDF export of the 5 report kinds |
| `SettingsPage.hpp` + `.cpp` | restaurant info, theme, backup/restore/export |
| `BillingDialog.hpp` + `.cpp` | bill breakdown, promo entry, payment method, tendered/change, receipt |
| `StatCard.hpp` + `.cpp` | animated dashboard statistic card |
| `Toast.hpp` + `.cpp` | `ToastHost` + `Toast` notification widgets |
| `CommandPalette.hpp` + `.cpp` | Ctrl+K global search over menu/customers/orders/actions |
| `PdfExporter.hpp` + `.cpp` | `QPdfWriter`/`QPrinter` receipts + report PDFs (keeps PrintSupport out of services) |
| `src/main.cpp` | entry point: theme → splash → context (multi-catch) → login → main window |

**Not lane-owned (orchestrator/integration):** `CMakeLists.txt`, `build.sh`, `assets/menu/menu_seed.json`,
`README.md`, remaining `docs/*.md`. `SchemaMigrator` consumes `assets/menu/menu_seed.json`.

---

## 3. Exact public interfaces

Transcribe these verbatim. Private members are listed so memory layout and const-ness are unambiguous.
Bodies shown inline (`{ ... }` with code) are the actual required implementations for trivial members;
`;` means implemented in the `.cpp`.

### 3.1 `aluchop::core`

#### `core/Money.hpp`

```cpp
#include <cstdint>
#include <iosfwd>
#include <QString>

namespace aluchop::core {

/// @oop-concept Operator Overloading :: money is a true value type — arithmetic/relational operators replace error-prone raw integers
class Money {
public:
    constexpr Money() noexcept = default;
    constexpr explicit Money(std::int64_t paisa) noexcept : m_paisa(paisa) {}

    /// @oop-concept Default Arguments :: paisa part defaults to 0 for whole-rupee amounts
    static constexpr Money fromRupees(std::int64_t rupees, std::int64_t paisa = 0) noexcept {
        return Money(rupees * 100 + paisa);
    }
    static constexpr Money zero() noexcept { return Money(); }

    /// @oop-concept Constant Member Functions :: all observers are const
    constexpr std::int64_t paisa() const noexcept { return m_paisa; }        // inline
    constexpr std::int64_t wholeRupees() const noexcept { return m_paisa / 100; }
    constexpr bool isZero() const noexcept { return m_paisa == 0; }
    constexpr bool isNegative() const noexcept { return m_paisa < 0; }

    QString toString() const;               // "Rs 1,250.00" — thousands-grouped, always 2 decimals
    Money percent(int pct) const noexcept;  // pct% of amount, rounded half-up (used by discounts/service charge)

    /// @oop-concept Return by Reference :: compound assignment returns *this for chaining
    Money& operator+=(Money rhs) noexcept { m_paisa += rhs.m_paisa; return *this; }
    Money& operator-=(Money rhs) noexcept { m_paisa -= rhs.m_paisa; return *this; }
    Money& operator*=(std::int64_t factor) noexcept { m_paisa *= factor; return *this; }

    /// @oop-concept Friend Function :: stream insertion needs the raw paisa representation
    friend std::ostream& operator<<(std::ostream& os, const Money& m);

private:
    std::int64_t m_paisa = 0;
};

// Free operators (all constexpr inline, implemented via paisa()):
constexpr Money operator+(Money a, Money b) noexcept { return Money(a.paisa() + b.paisa()); }
constexpr Money operator-(Money a, Money b) noexcept { return Money(a.paisa() - b.paisa()); }
constexpr Money operator-(Money a) noexcept { return Money(-a.paisa()); }
constexpr Money operator*(Money a, std::int64_t q) noexcept { return Money(a.paisa() * q); }
constexpr Money operator*(std::int64_t q, Money a) noexcept { return a * q; }
constexpr bool operator==(Money a, Money b) noexcept { return a.paisa() == b.paisa(); }
constexpr bool operator!=(Money a, Money b) noexcept { return !(a == b); }
constexpr bool operator<(Money a, Money b) noexcept { return a.paisa() < b.paisa(); }
constexpr bool operator<=(Money a, Money b) noexcept { return a.paisa() <= b.paisa(); }
constexpr bool operator>(Money a, Money b) noexcept { return b < a; }
constexpr bool operator>=(Money a, Money b) noexcept { return b <= a; }

/// @oop-concept Inline Functions :: explicit inline free helper used at every presentation edge
inline QString formatNpr(Money m) { return m.toString(); }

} // namespace aluchop::core
```

`Money.cpp` implements `toString()`, `percent()` (half-up: `(paisa*pct + 50) / 100` with sign care), and `operator<<` (writes `"NPR <rupees>.<paisa:02>"`).

#### `core/Result.hpp` (header-only)

```cpp
#include <optional>
#include <utility>
#include <stdexcept>
#include <QString>

namespace aluchop::core {

/// @oop-concept Class Template :: one generic success-or-error carrier for every service boundary
template <typename T>
class Result {
public:
    static Result ok(T value) { Result r; r.m_value = std::move(value); return r; }
    static Result err(QString message) { Result r; r.m_error = std::move(message); return r; }

    bool isOk() const noexcept { return m_value.has_value(); }
    explicit operator bool() const noexcept { return isOk(); }

    const T& value() const {                       // throws std::logic_error if err — programmer error
        if (!m_value) throw std::logic_error("Result::value() on error Result");
        return *m_value;
    }
    T& value() {
        if (!m_value) throw std::logic_error("Result::value() on error Result");
        return *m_value;
    }
    T take() { T v = std::move(value()); return v; }
    const QString& error() const noexcept { return m_error; }

    template <typename U>
    T valueOr(U&& fallback) const { return m_value ? *m_value : T(std::forward<U>(fallback)); }

private:
    Result() = default;
    std::optional<T> m_value;
    QString m_error;
};

template <>
class Result<void> {
public:
    static Result ok() { return Result(true, {}); }
    static Result err(QString message) { return Result(false, std::move(message)); }
    bool isOk() const noexcept { return m_ok; }
    explicit operator bool() const noexcept { return m_ok; }
    const QString& error() const noexcept { return m_error; }
private:
    Result(bool ok, QString e) : m_ok(ok), m_error(std::move(e)) {}
    bool m_ok = false;
    QString m_error;
};

} // namespace aluchop::core
```

#### `core/Exceptions.hpp` (header-only)

```cpp
#include <stdexcept>
#include <string>

namespace aluchop::core {

/// @oop-concept Custom Exception Hierarchy :: one catchable base for the whole application
class AluChopException : public std::runtime_error {
public:
    explicit AluChopException(const std::string& what) : std::runtime_error(what) {}
};

class DatabaseException : public AluChopException {
public:
    explicit DatabaseException(const std::string& what) : AluChopException("DB: " + what) {}
};
class ValidationException : public AluChopException {
public:
    explicit ValidationException(const std::string& what) : AluChopException("Validation: " + what) {}
};
class AuthException : public AluChopException {
public:
    explicit AuthException(const std::string& what) : AluChopException("Auth: " + what) {}
};
class InventoryException : public AluChopException {
public:
    explicit InventoryException(const std::string& what) : AluChopException("Inventory: " + what) {}
};
class FileIOException : public AluChopException {
public:
    explicit FileIOException(const std::string& what) : AluChopException("FileIO: " + what) {}
};

} // namespace aluchop::core
```

#### `core/Logger.hpp`

```cpp
#include <fstream>
#include <QString>

namespace aluchop::core {

/// @oop-concept Static Members :: process-wide singleton logger with a static message counter
class Logger {
public:
    enum class Level { Debug, Info, Warn, Error };

    static Logger& instance();                       // Meyers singleton — static member function

    void setLogFile(const QString& path);            // reopens std::ofstream in append mode; throws FileIOException

    /// @oop-concept Function Overloading :: same verb, two arities
    void log(const QString& message);                // defaults to Info
    void log(Level level, const QString& message);   // "[2026-08-01 12:00:00] [INFO] msg"

    void debug(const QString& m);
    void info(const QString& m);
    void warn(const QString& m);
    void error(const QString& m);

    static int messagesLogged() noexcept;            // reads s_messageCount

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();                                        // opens default "logs/aluchop.log" lazily
    ~Logger();                                       // flush + close (RAII)

    std::ofstream m_out;                             // append mode — /// @oop-concept File Handling (Append)
    QString m_path;
    static int s_messageCount;                       // static data member, defined in Logger.cpp
};

} // namespace aluchop::core
```

Every write checks `m_out.good()`; on failure it throws `FileIOException` (except inside the
destructor, which swallows).

#### `core/Algorithms.hpp` (header-only)

```cpp
#include "aluchop/core/Money.hpp"

namespace aluchop::core {

/// @oop-concept Function Template :: one summation used by billing, dashboards and reports alike
template <typename Container, typename Projection>
Money sumMoney(const Container& c, Projection proj) {
    Money total;
    for (auto it = c.begin(); it != c.end(); ++it)   // explicit iterators — STL iteration
        total += proj(*it);
    return total;
}

template <typename Container, typename Predicate>
int countMatching(const Container& c, Predicate pred) {
    int n = 0;
    for (const auto& e : c) if (pred(e)) ++n;
    return n;
}

template <typename T>
const T& clampValue(const T& v, const T& lo, const T& hi) {
    return v < lo ? lo : (hi < v ? hi : v);
}

} // namespace aluchop::core
```

#### `core/AppInfo.hpp` (header-only)

```cpp
#include <array>

namespace aluchop::core {

/// @oop-concept Structures :: plain aggregate for immutable app metadata
struct AppInfo {
    const char* appName;
    const char* version;
    const char* developer;
    const char* rollNo;
    const char* email;
};

/// @oop-concept Constant Objects :: compile-time app identity, used by footer, About and receipts
inline const AppInfo kAppInfo{
    "AluChop Restaurant Management System", "1.0.0",
    "Shashank Bhattarai", "ACE082BCT078", "shashankbhattarai006@gmail.com"
};

/// @oop-concept Object Arrays :: the 14 required categories as one const array — single source of truth
inline const std::array<const char*, 14> kMenuCategories{
    "Sushi", "Pizza", "Pasta", "Main Course", "Dimsum", "From the Tandoor", "From the Wok",
    "Bread & Rice", "Dessert", "Drinks", "Beer", "Wine", "Mocktails", "Shots"
};

} // namespace aluchop::core
```

### 3.2 `aluchop::models`

#### `models/Enums.hpp`

```cpp
#include <QString>

namespace aluchop::models {

/// @oop-concept Enumerations :: scoped enums for every closed domain vocabulary
enum class OrderType   { DineIn, Takeaway, Delivery };
enum class OrderStatus { Open, Pending, Preparing, Ready, Served, Paid, Cancelled };
enum class PaymentMethod { Cash, Card, Wallet };
enum class PromoKind   { Percent, Flat };
enum class ReservationStatus { Booked, Seated, Completed, Cancelled, NoShow };
enum class UserRole    { Admin, Manager, Waiter, Chef };
enum class NoticeLevel { Info, Success, Warning, Danger };

// DB-string mapping (exact tokens used in the SQLite CHECK constraints, §6):
QString toString(OrderType v);          OrderType   orderTypeFromString(const QString& s);
QString toString(OrderStatus v);        OrderStatus orderStatusFromString(const QString& s);
QString toString(PaymentMethod v);      PaymentMethod paymentMethodFromString(const QString& s);
QString toString(PromoKind v);          PromoKind   promoKindFromString(const QString& s);
QString toString(ReservationStatus v);  ReservationStatus reservationStatusFromString(const QString& s);
QString toString(UserRole v);           UserRole    userRoleFromString(const QString& s);
// each *FromString throws core::ValidationException on unknown token
```

#### `models/Interfaces.hpp` (header-only)

```cpp
#include <QString>
#include <QJsonObject>
#include "aluchop/core/Money.hpp"

namespace aluchop::models {

/// @oop-concept Abstract Classes / Pure Virtual Functions :: capability mixins with no state
class IPrintable {
public:
    virtual ~IPrintable() = default;
    virtual QString toPrintableText() const = 0;    // monospace receipt/summary body
};

class ISerializable {
public:
    virtual ~ISerializable() = default;
    virtual QJsonObject toJson() const = 0;
    virtual void fromJson(const QJsonObject& obj) = 0;   // throws core::ValidationException
};

class IAuditable {
public:
    virtual ~IAuditable() = default;
    virtual QString auditDescription() const = 0;   // one line for the audit trail
};

class IDiscountable {
public:
    virtual ~IDiscountable() = default;
    virtual void setDiscount(core::Money amount, const QString& label) = 0;
    virtual core::Money discount() const = 0;
};

} // namespace aluchop::models
```

#### `models/Person.hpp` — root of the diamond

```cpp
#include <QString>

namespace aluchop::models {

/// @oop-concept Abstract Classes :: nobody is "just a Person" in the restaurant — always a role
class Person {
public:
    Person() = default;
    /// @oop-concept Parameterised Constructor
    Person(int id, QString name, QString phone, QString email);
    virtual ~Person() = default;

    virtual QString roleName() const = 0;            // pure virtual
    virtual QString displayLabel() const;            // default: "<name> (<roleName()>)" — overridable

    int id() const noexcept { return m_id; }
    void setId(int id) { m_id = id; }

    /// @oop-concept Return by Reference :: identity fields exposed as const refs, no copies
    const QString& name() const noexcept { return m_name; }
    const QString& phone() const noexcept { return m_phone; }
    const QString& email() const noexcept { return m_email; }

    void setName(const QString& v);      // throws core::ValidationException if trimmed-empty
    void setPhone(const QString& v);     // throws if non-empty and not 7–15 digits
    void setEmail(const QString& v);     // throws if non-empty and missing '@'

protected:
    int m_id = 0;
    QString m_name;
    QString m_phone;
    QString m_email;
};

} // namespace aluchop::models
```

#### `models/Employee.hpp`

```cpp
#include <QDate>
#include "aluchop/models/Person.hpp"
#include "aluchop/core/Money.hpp"

namespace aluchop::models {

/// @oop-concept Virtual Base Class :: Employee virtually derives Person so StaffCustomer holds ONE identity
class Employee : virtual public Person {
public:
    Employee() = default;
    Employee(int id, QString name, QString phone, QString email,
             QString position, core::Money monthlySalary, QString shift);

    QString roleName() const override;               // "Employee"

    /// @oop-concept Virtual Functions :: payroll is computed polymorphically per role
    virtual core::Money monthlyPay() const;          // base: m_salary

    const QString& position() const noexcept { return m_position; }
    void setPosition(const QString& v);
    core::Money salary() const noexcept { return m_salary; }
    void setSalary(core::Money v);                   // throws ValidationException if negative
    const QString& shift() const noexcept { return m_shift; }
    void setShift(const QString& v);
    QDate hiredDate() const noexcept { return m_hired; }
    void setHiredDate(QDate d) { m_hired = d; }
    bool isActive() const noexcept { return m_active; }
    void setActive(bool a) { m_active = a; }
    int performanceRating() const noexcept { return m_rating; }
    void setPerformanceRating(int r);                // clamped 1..5 via core::clampValue

protected:
    QString m_position;
    core::Money m_salary;
    QString m_shift = QStringLiteral("DAY");
    QDate m_hired;
    bool m_active = true;
    int m_rating = 3;
};

} // namespace aluchop::models
```

#### `models/Customer.hpp`

```cpp
#include <QDateTime>
#include "aluchop/models/Person.hpp"

namespace aluchop::models {

class Customer : virtual public Person {             // second virtual edge of the diamond
public:
    Customer() = default;
    Customer(int id, QString name, QString phone, QString email);

    QString roleName() const override;               // "Customer"

    int loyaltyPoints() const noexcept { return m_loyaltyPoints; }
    void addLoyaltyPoints(int pts);                  // throws ValidationException if pts < 0
    void redeemPoints(int pts);                      // throws ValidationException if pts > balance
    int visits() const noexcept { return m_visits; }
    QDateTime createdAt() const noexcept { return m_created; }
    void setCreatedAt(QDateTime t) { m_created = t; }
    void setLoyaltyPoints(int p) { m_loyaltyPoints = p; }   // repo hydration only
    void setVisits(int v) { m_visits = v; }                 // repo hydration only

    /// @oop-concept Increment Operator :: "one more visit" is the domain's natural ++ —
    /// prefix and postfix, used by CustomerService::recordVisit
    Customer& operator++();                          // ++c : m_visits += 1, returns *this
    Customer operator++(int);                        // c++ : returns pre-increment copy

private:
    int m_loyaltyPoints = 0;
    int m_visits = 0;
    QDateTime m_created;
};

} // namespace aluchop::models
```

#### `models/Waiter.hpp`, `models/Chef.hpp`, `models/Manager.hpp`

```cpp
// Waiter.hpp
#include "aluchop/models/Employee.hpp"
namespace aluchop::models {

/// @oop-concept Hierarchical Inheritance :: Waiter/Chef/Manager all specialise Employee
class Waiter : public Employee {
public:
    Waiter() = default;
    Waiter(int id, QString name, QString phone, QString email,
           core::Money monthlySalary, QString shift);
    QString roleName() const override;               // "Waiter"
    /// @oop-concept Method Overriding :: waiter pay = salary + tips
    core::Money monthlyPay() const override;         // m_salary + m_tipsThisMonth
    core::Money tipsThisMonth() const noexcept { return m_tips; }
    void addTip(core::Money t);                      // throws ValidationException if negative
    int tablesServed() const noexcept { return m_tablesServed; }
    void setTablesServed(int n) { m_tablesServed = n; }
private:
    core::Money m_tips;
    int m_tablesServed = 0;
};
} // namespace aluchop::models

// Chef.hpp
#include "aluchop/models/Employee.hpp"
namespace aluchop::models {
class Chef : public Employee {
public:
    Chef() = default;
    Chef(int id, QString name, QString phone, QString email,
         core::Money monthlySalary, QString shift, QString specialty = QString());
    QString roleName() const override;               // "Chef"
    core::Money monthlyPay() const override;         // m_salary + overtimeHours * kOvertimeRatePerHour
    const QString& specialty() const noexcept { return m_specialty; }
    void setSpecialty(const QString& s) { m_specialty = s; }
    int overtimeHours() const noexcept { return m_overtimeHours; }
    void setOvertimeHours(int h);                    // throws ValidationException if negative
    static const core::Money kOvertimeRatePerHour;   // Rs 300/hr, defined in Chef.cpp
private:
    QString m_specialty;
    int m_overtimeHours = 0;
};
} // namespace aluchop::models

// Manager.hpp
#include "aluchop/models/Employee.hpp"
namespace aluchop::models {
/// @oop-concept Single Inheritance :: Manager extends exactly one concrete base
class Manager : public Employee {
public:
    Manager() = default;
    Manager(int id, QString name, QString phone, QString email,
            core::Money monthlySalary, QString shift, core::Money monthlyBonus = core::Money());
    QString roleName() const override;               // "Manager"
    core::Money monthlyPay() const override;         // m_salary + m_bonus
    core::Money monthlyBonus() const noexcept { return m_bonus; }
    void setMonthlyBonus(core::Money b);
protected:
    core::Money m_bonus;
};
} // namespace aluchop::models
```

#### `models/Admin.hpp`

```cpp
#include "aluchop/models/Manager.hpp"
#include "aluchop/models/Interfaces.hpp"

namespace aluchop::models {

/// @oop-concept Multilevel Inheritance :: Person → Employee → Manager → Admin
/// @oop-concept Multiple Inheritance :: concrete base + capability interface
class Admin : public Manager, public IAuditable {
public:
    Admin() = default;
    Admin(int id, QString name, QString phone, QString email,
          core::Money monthlySalary, QString shift);
    QString roleName() const final;                  // "Admin" — final: no role may masquerade below Admin
    QString auditDescription() const override;       // "Admin <name> (id <id>)"
    bool canManageUsers() const noexcept { return true; }
};

} // namespace aluchop::models
```

#### `models/StaffCustomer.hpp` — the diamond

```cpp
#include "aluchop/models/Employee.hpp"
#include "aluchop/models/Customer.hpp"

namespace aluchop::models {

/// @oop-concept Hybrid Inheritance / Virtual Base Class :: a staff member who is also a loyalty
/// customer — without `virtual public Person` this object would carry two names and two ids.
class StaffCustomer : public Employee, public Customer {
public:
    StaffCustomer() = default;
    // The most-derived class constructs the virtual base directly:
    StaffCustomer(int personId, QString name, QString phone, QString email,
                  QString position, core::Money monthlySalary, QString shift);
    QString roleName() const override;               // "Staff Member" — resolves the diamond ambiguity
    QString displayLabel() const override;           // "<name> (Staff · <loyaltyPoints> pts)"
    int staffDiscountPercent() const noexcept { return 10; }
};

} // namespace aluchop::models
```

Constructor rule (binding): `StaffCustomer`'s ctor initialiser list is
`Person(personId, ...), Employee(...), Customer(...)` — the `Person(...)` calls inside
`Employee`/`Customer` ctors are ignored for virtual bases; only the most-derived call constructs `Person`.

#### `models/User.hpp`

```cpp
#include <QString>
#include <QDateTime>
#include "aluchop/models/Enums.hpp"

namespace aluchop::models {

class User {
public:
    User() = default;
    User(int id, QString username, UserRole role);

    int id() const noexcept { return m_id; }                 void setId(int v) { m_id = v; }
    const QString& username() const noexcept { return m_username; }
    void setUsername(const QString& v);                      // throws ValidationException if empty
    UserRole role() const noexcept { return m_role; }        void setRole(UserRole r) { m_role = r; }
    const QString& passHash() const noexcept { return m_passHash; }
    void setPassHash(const QString& v) { m_passHash = v; }
    const QString& salt() const noexcept { return m_salt; }  void setSalt(const QString& v) { m_salt = v; }
    int employeeId() const noexcept { return m_employeeId; } void setEmployeeId(int v) { m_employeeId = v; }
    const QString& securityQuestion() const noexcept { return m_securityQuestion; }
    void setSecurityQuestion(const QString& v) { m_securityQuestion = v; }
    const QString& securityAnswerHash() const noexcept { return m_securityAnswerHash; }
    void setSecurityAnswerHash(const QString& v) { m_securityAnswerHash = v; }
    const QString& rememberToken() const noexcept { return m_rememberToken; }
    void setRememberToken(const QString& v) { m_rememberToken = v; }

private:
    int m_id = 0;
    QString m_username;
    UserRole m_role = UserRole::Waiter;
    QString m_passHash, m_salt;
    int m_employeeId = 0;                            // 0 = no linked employee
    QString m_securityQuestion, m_securityAnswerHash, m_rememberToken;
};

} // namespace aluchop::models
```

#### `models/MenuItem.hpp`

```cpp
#include <QString>
#include "aluchop/models/Interfaces.hpp"
#include "aluchop/core/Money.hpp"

namespace aluchop::models {

class MenuItem : public ISerializable {
public:
    MenuItem() = default;
    MenuItem(int id, QString name, QString category, core::Money price, QString description);

    int id() const noexcept { return m_id; }                     void setId(int v) { m_id = v; }
    const QString& name() const noexcept { return m_name; }      void setName(const QString& v);       // non-empty
    const QString& category() const noexcept { return m_category; }
    void setCategory(const QString& v);              // must be one of core::kMenuCategories, else ValidationException
    core::Money price() const noexcept { return m_price; }
    void setPrice(core::Money p);                    // throws ValidationException if negative
    const QString& description() const noexcept { return m_description; }
    void setDescription(const QString& v) { m_description = v; }
    const QString& imagePath() const noexcept { return m_imagePath; }
    void setImagePath(const QString& v) { m_imagePath = v; }
    bool isAvailable() const noexcept { return m_available; }
    void setAvailable(bool a) { m_available = a; }

    QJsonObject toJson() const override;             // keys: name, category, price_paisa, description, image, available
    void fromJson(const QJsonObject& obj) override;  // used by SchemaMigrator seeding

    friend bool operator==(const MenuItem& a, const MenuItem& b) { return a.m_id == b.m_id; }
    bool operator<(const MenuItem& rhs) const;       // by name, case-insensitive — default sort

private:
    int m_id = 0;
    QString m_name, m_category, m_description, m_imagePath;
    core::Money m_price;
    bool m_available = true;
};

} // namespace aluchop::models
```

#### `models/OrderItem.hpp`

```cpp
#include <QString>
#include "aluchop/core/Money.hpp"

namespace aluchop::models {

class OrderItem {
public:
    OrderItem() = default;
    OrderItem(int menuItemId, QString nameSnapshot, core::Money unitPrice, int qty);

    int menuItemId() const noexcept { return m_menuItemId; }
    const QString& name() const noexcept { return m_name; }        // frozen at order time
    core::Money unitPrice() const noexcept { return m_unitPrice; } // frozen at order time
    int qty() const noexcept { return m_qty; }
    void setQty(int q);                              // throws ValidationException if q < 1
    const QString& note() const noexcept { return m_note; }
    void setNote(const QString& n) { m_note = n; }
    core::Money lineTotal() const noexcept { return m_unitPrice * m_qty; }

private:
    int m_menuItemId = 0;
    QString m_name;
    core::Money m_unitPrice;
    int m_qty = 1;
    QString m_note;
};

} // namespace aluchop::models
```

#### `models/Order.hpp`

```cpp
#include <vector>
#include <cstddef>
#include <QString>
#include <QDateTime>
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/Interfaces.hpp"
#include "aluchop/models/OrderItem.hpp"
#include "aluchop/core/Money.hpp"

namespace aluchop::models {

class Order : public IPrintable {
public:
    Order();                                          // ++s_openCount
    Order(int id, QString orderNumber, OrderType type,
          int tableId, int customerId, int waiterId, QDateTime createdAt);

    /// @oop-concept Copy Constructor :: split-bill genuinely copies an order —
    /// the copy gets id 0 / empty number (a new, unsaved order) and its OWN item vector
    Order(const Order& other);
    /// @oop-concept Assignment Operator :: same deep-copy semantics as the copy ctor
    Order& operator=(const Order& other);
    /// @oop-concept Destructor :: maintains the live open-order counter
    ~Order() override;                                // --s_openCount

    // --- items -------------------------------------------------------------
    /// @oop-concept Function Overloading :: add a prepared line OR build one in place
    void addItem(const OrderItem& item);              // merges qty if same menuItemId
    void addItem(int menuItemId, const QString& name, core::Money unitPrice, int qty);
    void removeItemAt(std::size_t index);             // throws std::out_of_range
    std::size_t itemCount() const noexcept { return m_items.size(); }
    const std::vector<OrderItem>& items() const noexcept { return m_items; }

    /// @oop-concept Subscript Operator / Return by Reference :: an order IS a sequence of lines
    OrderItem& operator[](std::size_t i);             // throws std::out_of_range
    const OrderItem& operator[](std::size_t i) const;

    /// @oop-concept Operator Overloading :: merging bills is the domain meaning of +=
    Order& operator+=(const Order& other);            // appends/merges other's items into *this

    core::Money subtotal() const;                     // core::sumMoney over lineTotal()

    // --- lifecycle ---------------------------------------------------------
    OrderStatus status() const noexcept { return m_status; }
    void setStatus(OrderStatus s);                    // validates legal transition, else ValidationException
    // Legal: Open→{Pending,Cancelled}; Pending→{Preparing,Cancelled}; Preparing→Ready;
    //        Ready→Served; Served→Paid. Anything else throws.

    // --- identity / metadata ----------------------------------------------
    int id() const noexcept { return m_id; }                     void setId(int v) { m_id = v; }
    const QString& orderNumber() const noexcept { return m_orderNumber; }
    void setOrderNumber(const QString& v) { m_orderNumber = v; }
    OrderType type() const noexcept { return m_type; }           void setType(OrderType t) { m_type = t; }
    int tableId() const noexcept { return m_tableId; }           void setTableId(int v) { m_tableId = v; }
    int customerId() const noexcept { return m_customerId; }     void setCustomerId(int v) { m_customerId = v; }
    int waiterId() const noexcept { return m_waiterId; }         void setWaiterId(int v) { m_waiterId = v; }
    QDateTime createdAt() const noexcept { return m_createdAt; } void setCreatedAt(QDateTime t) { m_createdAt = t; }
    const QString& note() const noexcept { return m_note; }      void setNote(const QString& n) { m_note = n; }

    QString toPrintableText() const override;         // kitchen ticket text

    /// @oop-concept Static Members :: how many Order objects are alive right now (dashboard uses it)
    static int openOrderCount() noexcept;

private:
    int m_id = 0;
    QString m_orderNumber;
    OrderType m_type = OrderType::DineIn;
    OrderStatus m_status = OrderStatus::Open;
    int m_tableId = 0, m_customerId = 0, m_waiterId = 0;
    QDateTime m_createdAt;
    QString m_note;
    std::vector<OrderItem> m_items;                   // /// @oop-concept Objects as Members
    static int s_openCount;                           // defined in Order.cpp
};

} // namespace aluchop::models
```

#### `models/Bill.hpp`

```cpp
#include <vector>
#include <iosfwd>
#include <QString>
#include "aluchop/models/Interfaces.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/OrderItem.hpp"
#include "aluchop/core/Money.hpp"

namespace aluchop { namespace services { class BillingService; } }

namespace aluchop::models {

/// @oop-concept Multiple Inheritance :: a bill is printable AND discountable
class Bill : public IPrintable, public IDiscountable {
    /// @oop-concept Friend Class :: ONLY BillingService may settle a bill — the mutators are private
    friend class aluchop::services::BillingService;
public:
    Bill() = default;
    explicit Bill(const Order& order);                // snapshots items, subtotal, ids

    int orderId() const noexcept { return m_orderId; }
    const QString& orderNumber() const noexcept { return m_orderNumber; }
    const std::vector<OrderItem>& items() const noexcept { return m_items; }

    core::Money subtotal() const noexcept { return m_subtotal; }
    core::Money discount() const override { return m_discount; }
    void setDiscount(core::Money amount, const QString& label) override;
        // throws ValidationException if negative or > subtotal
    const QString& discountLabel() const noexcept { return m_discountLabel; }
    core::Money serviceCharge() const noexcept { return m_serviceCharge; }
    void setServiceCharge(core::Money v);             // throws ValidationException if negative
    const QString& promoCode() const noexcept { return m_promoCode; }
    void setPromoCode(const QString& c) { m_promoCode = c; }

    /// NOTE: prices are tax-INCLUSIVE. total NEVER adds tax.
    core::Money total() const noexcept { return m_subtotal - m_discount + m_serviceCharge; }

    bool isSettled() const noexcept { return m_settled; }
    PaymentMethod method() const noexcept { return m_method; }        // valid only if settled
    core::Money tendered() const noexcept { return m_tendered; }
    core::Money change() const noexcept { return m_change; }

    QString toPrintableText() const override;         // full receipt incl. "All prices are inclusive of tax."

    /// @oop-concept Stream Insertion Operator :: bills stream themselves into text receipts / CSV
    friend std::ostream& operator<<(std::ostream& os, const Bill& b);

private:
    void settle(PaymentMethod m, core::Money tendered, core::Money change);  // friend-only

    int m_orderId = 0;
    QString m_orderNumber;
    std::vector<OrderItem> m_items;
    core::Money m_subtotal, m_discount, m_serviceCharge, m_tendered, m_change;
    QString m_discountLabel, m_promoCode;
    PaymentMethod m_method = PaymentMethod::Cash;
    bool m_settled = false;
};

} // namespace aluchop::models
```

#### `models/Payment.hpp`

```cpp
#include <QDateTime>
#include "aluchop/models/Enums.hpp"
#include "aluchop/core/Money.hpp"

namespace aluchop::models {

class Payment {
public:
    Payment() = default;

    int id() const noexcept { return m_id; }                     void setId(int v) { m_id = v; }
    int orderId() const noexcept { return m_orderId; }           void setOrderId(int v) { m_orderId = v; }
    PaymentMethod method() const noexcept { return m_method; }   void setMethod(PaymentMethod m) { m_method = m; }
    core::Money subtotal() const noexcept { return m_subtotal; } void setSubtotal(core::Money v) { m_subtotal = v; }
    core::Money discount() const noexcept { return m_discount; } void setDiscount(core::Money v) { m_discount = v; }
    core::Money serviceCharge() const noexcept { return m_serviceCharge; }
    void setServiceCharge(core::Money v) { m_serviceCharge = v; }
    core::Money total() const noexcept { return m_total; }       void setTotal(core::Money v) { m_total = v; }
    core::Money tendered() const noexcept { return m_tendered; } void setTendered(core::Money v) { m_tendered = v; }
    core::Money change() const noexcept { return m_change; }     void setChange(core::Money v) { m_change = v; }
    int promoId() const noexcept { return m_promoId; }           void setPromoId(int v) { m_promoId = v; }
    int receivedByUserId() const noexcept { return m_receivedBy; }
    void setReceivedByUserId(int v) { m_receivedBy = v; }
    QDateTime paidAt() const noexcept { return m_paidAt; }       void setPaidAt(QDateTime t) { m_paidAt = t; }

private:
    int m_id = 0, m_orderId = 0, m_promoId = 0, m_receivedBy = 0;
    PaymentMethod m_method = PaymentMethod::Cash;
    core::Money m_subtotal, m_discount, m_serviceCharge, m_total, m_tendered, m_change;
    QDateTime m_paidAt;
};

} // namespace aluchop::models
```

#### `models/Ingredient.hpp`, `models/Supplier.hpp`, `models/RecipeLine.hpp`

```cpp
// Ingredient.hpp
#include <QString>
#include <QDate>
#include "aluchop/core/Money.hpp"
namespace aluchop::models {
class Ingredient {
public:
    Ingredient() = default;
    Ingredient(int id, QString name, QString unit, double stockQty, double lowThreshold);
    int id() const noexcept { return m_id; }                     void setId(int v) { m_id = v; }
    const QString& name() const noexcept { return m_name; }      void setName(const QString& v);   // non-empty
    const QString& unit() const noexcept { return m_unit; }      void setUnit(const QString& v);   // non-empty ("kg","l","pcs")
    double stockQty() const noexcept { return m_stockQty; }      void setStockQty(double v);       // throws if negative
    double lowThreshold() const noexcept { return m_lowThreshold; }
    void setLowThreshold(double v);                              // throws if negative
    QDate expiryDate() const noexcept { return m_expiry; }       void setExpiryDate(QDate d) { m_expiry = d; }
    core::Money unitCost() const noexcept { return m_unitCost; } void setUnitCost(core::Money c);  // throws if negative
    int supplierId() const noexcept { return m_supplierId; }     void setSupplierId(int v) { m_supplierId = v; }
    bool isLow() const noexcept { return m_stockQty <= m_lowThreshold; }
    bool expiresWithin(int days) const;              // false when no expiry set
private:
    int m_id = 0, m_supplierId = 0;
    QString m_name, m_unit;
    double m_stockQty = 0.0, m_lowThreshold = 0.0;   // physical quantity, NOT money — double allowed
    QDate m_expiry;
    core::Money m_unitCost;
};
} // namespace aluchop::models

// Supplier.hpp
#include <QString>
namespace aluchop::models {
class Supplier {
public:
    Supplier() = default;
    Supplier(int id, QString name, QString phone, QString email, QString address);
    int id() const noexcept { return m_id; }                 void setId(int v) { m_id = v; }
    const QString& name() const noexcept { return m_name; }  void setName(const QString& v);  // non-empty
    const QString& phone() const noexcept { return m_phone; }   void setPhone(const QString& v) { m_phone = v; }
    const QString& email() const noexcept { return m_email; }   void setEmail(const QString& v) { m_email = v; }
    const QString& address() const noexcept { return m_address; } void setAddress(const QString& v) { m_address = v; }
private:
    int m_id = 0;
    QString m_name, m_phone, m_email, m_address;
};
} // namespace aluchop::models

// RecipeLine.hpp (header-only)
namespace aluchop::models {
/// @oop-concept Structures :: pure data link between a dish and an ingredient
struct RecipeLine {
    int menuItemId = 0;
    int ingredientId = 0;
    double qtyPerServing = 0.0;
};
} // namespace aluchop::models
```

#### `models/Reservation.hpp`, `models/Table.hpp`, `models/Promo.hpp`

```cpp
// Reservation.hpp
#include <QString>
#include <QDateTime>
#include "aluchop/models/Enums.hpp"
namespace aluchop::models {
class Reservation {
public:
    Reservation() = default;
    Reservation(int id, int tableId, QString customerName, QString phone,
                QDateTime startsAt, int durationMin, int guests);
    int id() const noexcept { return m_id; }                     void setId(int v) { m_id = v; }
    int tableId() const noexcept { return m_tableId; }           void setTableId(int v) { m_tableId = v; }
    int customerId() const noexcept { return m_customerId; }     void setCustomerId(int v) { m_customerId = v; }
    const QString& customerName() const noexcept { return m_customerName; }
    void setCustomerName(const QString& v);                      // non-empty
    const QString& phone() const noexcept { return m_phone; }    void setPhone(const QString& v) { m_phone = v; }
    QDateTime startsAt() const noexcept { return m_startsAt; }   void setStartsAt(QDateTime t) { m_startsAt = t; }
    int durationMin() const noexcept { return m_durationMin; }   void setDurationMin(int v);   // throws if < 15
    QDateTime endsAt() const { return m_startsAt.addSecs(60LL * m_durationMin); }
    int guests() const noexcept { return m_guests; }             void setGuests(int v);        // throws if < 1
    const QString& specialRequest() const noexcept { return m_specialRequest; }
    void setSpecialRequest(const QString& v) { m_specialRequest = v; }
    ReservationStatus status() const noexcept { return m_status; }
    void setStatus(ReservationStatus s) { m_status = s; }
    bool overlaps(const QDateTime& start, int durationMin) const;   // half-open interval test
private:
    int m_id = 0, m_tableId = 0, m_customerId = 0, m_durationMin = 90, m_guests = 1;
    QString m_customerName, m_phone, m_specialRequest;
    QDateTime m_startsAt;
    ReservationStatus m_status = ReservationStatus::Booked;
};
} // namespace aluchop::models

// Table.hpp
#include <QString>
namespace aluchop::models {
class Table {
public:
    Table() = default;
    Table(int id, QString name, int capacity);
    int id() const noexcept { return m_id; }                 void setId(int v) { m_id = v; }
    const QString& name() const noexcept { return m_name; }  void setName(const QString& v);  // non-empty
    int capacity() const noexcept { return m_capacity; }     void setCapacity(int c);         // throws if < 1
    bool isActive() const noexcept { return m_active; }      void setActive(bool a) { m_active = a; }
private:
    int m_id = 0, m_capacity = 2;
    QString m_name;
    bool m_active = true;
};
} // namespace aluchop::models

// Promo.hpp
#include <QString>
#include <QDate>
#include "aluchop/models/Enums.hpp"
#include "aluchop/core/Money.hpp"
namespace aluchop::models {
class Promo {
public:
    Promo() = default;
    int id() const noexcept { return m_id; }                     void setId(int v) { m_id = v; }
    const QString& code() const noexcept { return m_code; }      void setCode(const QString& v);  // non-empty, uppercased
    PromoKind kind() const noexcept { return m_kind; }           void setKind(PromoKind k) { m_kind = k; }
    int percent() const noexcept { return m_percent; }           void setPercent(int p);          // 0..100
    core::Money flatAmount() const noexcept { return m_flat; }   void setFlatAmount(core::Money v);   // >= 0
    core::Money minOrder() const noexcept { return m_minOrder; } void setMinOrder(core::Money v) { m_minOrder = v; }
    QDate validFrom() const noexcept { return m_validFrom; }     void setValidFrom(QDate d) { m_validFrom = d; }
    QDate validTo() const noexcept { return m_validTo; }         void setValidTo(QDate d) { m_validTo = d; }
    bool isActive() const noexcept { return m_active; }          void setActive(bool a) { m_active = a; }
    bool isValidOn(QDate day, core::Money orderSubtotal) const;  // active + window + min order
    core::Money discountFor(core::Money subtotal) const;         // percent→subtotal.percent(p); flat→min(flat, subtotal)
private:
    int m_id = 0, m_percent = 0;
    QString m_code;
    PromoKind m_kind = PromoKind::Percent;
    core::Money m_flat, m_minOrder;
    QDate m_validFrom, m_validTo;
    bool m_active = true;
};
} // namespace aluchop::models
```

### 3.3 `aluchop::persistence`

#### `persistence/Database.hpp`

```cpp
#include <functional>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariantList>
#include <QString>

namespace aluchop::persistence {

/// @oop-concept Static Members :: exactly one DB connection, owned by a Meyers singleton
class Database {
public:
    static Database& instance();

    void open(const QString& dbFilePath);            // opens QSQLITE, PRAGMA foreign_keys=ON; throws DatabaseException
    void close();
    bool isOpen() const;
    QString filePath() const { return m_path; }

    /// @oop-concept Return by Reference :: repositories borrow the live handle, never copy it
    QSqlDatabase& handle();

    QSqlQuery exec(const QString& sql);                                   // throws DatabaseException on failure
    QSqlQuery prepared(const QString& sql, const QVariantList& binds);    // positional '?' binds; throws

    /// @oop-concept Rethrow :: rolls back on ANY exception, then `throw;` so callers still see it
    void transaction(const std::function<void()>& body);

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

private:
    Database() = default;
    ~Database();                                     // closes connection (RAII)
    QString m_path;
    QSqlDatabase m_db;                               // named connection "aluchop_main"
};

} // namespace aluchop::persistence
```

#### `persistence/SchemaMigrator.hpp`

```cpp
#include <QString>

namespace aluchop::persistence {

class SchemaMigrator {
public:
    explicit SchemaMigrator(Database& db);
    void migrate(const QString& menuSeedJsonPath);   // applies pending migrations, then seeds on first run
    int currentVersion() const;                      // settings.schema_version, 0 if absent
    static constexpr int kLatestVersion = 1;
private:
    void applyMigration1();                          // full DDL of §6, inside db.transaction
    void seedIfEmpty(const QString& menuSeedJsonPath);   // menu JSON, admin user, tables, suppliers, ingredients, recipes, promos
    Database& m_db;
};

} // namespace aluchop::persistence
```

#### `persistence/Repository.hpp` (header-only class template)

```cpp
#include <optional>
#include <vector>
#include <QSqlRecord>
#include <QSqlQuery>
#include <QString>
#include <QVariantList>
#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {

/// @oop-concept Class Template :: one generic CRUD skeleton, specialised per entity by fromRecord()
template <typename T>
class Repository {
public:
    explicit Repository(QString tableName) : m_table(std::move(tableName)) {}
    virtual ~Repository() = default;

    std::vector<T> findAll() const {
        std::vector<T> out;
        QSqlQuery q = Database::instance().exec(
            QStringLiteral("SELECT * FROM %1 ORDER BY %2").arg(m_table, orderByClause()));
        while (q.next()) out.push_back(fromRecord(q.record()));
        return out;
    }
    std::optional<T> findById(int id) const {
        QSqlQuery q = Database::instance().prepared(
            QStringLiteral("SELECT * FROM %1 WHERE id = ?").arg(m_table), { id });
        if (q.next()) return fromRecord(q.record());
        return std::nullopt;
    }
    int count() const {
        QSqlQuery q = Database::instance().exec(
            QStringLiteral("SELECT COUNT(*) FROM %1").arg(m_table));
        return q.next() ? q.value(0).toInt() : 0;
    }
    void removeById(int id) {
        Database::instance().prepared(
            QStringLiteral("DELETE FROM %1 WHERE id = ?").arg(m_table), { id });
    }

protected:
    virtual T fromRecord(const QSqlRecord& rec) const = 0;   // pure virtual hydration hook
    virtual QString orderByClause() const { return QStringLiteral("id"); }
    const QString m_table;
};

} // namespace aluchop::persistence
```

#### Concrete repositories

All derive `public Repository<Model>`, override `fromRecord` (protected, `override`), and add the
methods below. Constructors are all default (`ClassName() : Repository(QStringLiteral("<table>")) {}`).

```cpp
// UserRepository.hpp        — table "users"
class UserRepository : public Repository<models::User> {
public:
    UserRepository();
    int insert(const models::User& u);                       // returns new id
    void update(const models::User& u);
    std::optional<models::User> byUsername(const QString& username) const;
    std::optional<models::User> byRememberToken(const QString& token) const;
    void setRememberToken(int userId, const QString& token); // empty clears
    void setPassword(int userId, const QString& hash, const QString& salt);
protected:
    models::User fromRecord(const QSqlRecord& rec) const override;
};

// MenuRepository.hpp        — tables "menu_items" + "recipes"
class MenuRepository : public Repository<models::MenuItem> {
public:
    MenuRepository();
    int insert(const models::MenuItem& item);
    void update(const models::MenuItem& item);
    std::vector<models::MenuItem> byCategory(const QString& category) const;
    std::vector<models::MenuItem> search(const QString& term) const;   // LIKE on name+description
    void setAvailability(int itemId, bool available);
    std::vector<models::RecipeLine> recipeFor(int menuItemId) const;
    void setRecipe(int menuItemId, const std::vector<models::RecipeLine>& lines);  // replace-all, in one transaction
protected:
    models::MenuItem fromRecord(const QSqlRecord& rec) const override;
    QString orderByClause() const override;                  // "category, name"
};

// CustomerRepository.hpp    — table "customers"
class CustomerRepository : public Repository<models::Customer> {
public:
    CustomerRepository();
    int insert(const models::Customer& c);
    void update(const models::Customer& c);                  // writes loyalty_points + visits too
    std::optional<models::Customer> byPhone(const QString& phone) const;
    std::vector<models::Customer> search(const QString& term) const;   // LIKE on name+phone
protected:
    models::Customer fromRecord(const QSqlRecord& rec) const override;
    QString orderByClause() const override;                  // "name"
};

// EmployeeRepository.hpp    — tables "employees" + "attendance"
// Hydrates the POLYMORPHIC role object: position column decides Waiter/Chef/Manager/Admin.
class EmployeeRepository : public Repository<models::Employee> {   // T = Employee (base slice for lists)
public:
    EmployeeRepository();
    int insert(const models::Employee& e);                   // position from e.position()
    void update(const models::Employee& e);
    /// @oop-concept Dynamic Objects / Runtime Polymorphism :: factory builds the concrete role on the heap
    std::unique_ptr<models::Employee> makeTyped(int employeeId) const;   // nullptr if absent
    std::vector<std::unique_ptr<models::Employee>> allTyped() const;
    void markAttendance(int employeeId, QDate day, const QString& status,
                        QTime checkIn = QTime(), QTime checkOut = QTime());   // UPSERT
    // returns rows (work_date, status, check_in, check_out) for the month:
    std::vector<std::tuple<QDate, QString, QTime, QTime>> attendanceFor(int employeeId, int year, int month) const;
protected:
    models::Employee fromRecord(const QSqlRecord& rec) const override;   // returns base-sliced Employee
    QString orderByClause() const override;                  // "name"
};

// OrderRepository.hpp       — tables "orders" + "order_items"
class OrderRepository : public Repository<models::Order> {
public:
    OrderRepository();
    int insertOrder(models::Order& o);                       // one transaction: order + items; sets o.id + number "ORD-YYYYMMDD-NNN"
    void updateOrder(const models::Order& o);                // rewrites header + replaces items
    void updateStatus(int orderId, models::OrderStatus s);
    std::optional<models::Order> loadOrder(int orderId) const;   // header + items hydrated
    std::vector<models::Order> withStatus(models::OrderStatus s) const;
    std::vector<models::Order> activeOrders() const;         // status NOT IN (Paid, Cancelled)
    std::vector<models::Order> between(const QDateTime& from, const QDateTime& to) const;
    std::vector<models::Order> forCustomer(int customerId, int limit = 20) const;
    void markMergedInto(int sourceOrderId, int targetOrderId);
protected:
    models::Order fromRecord(const QSqlRecord& rec) const override;   // header only; loadOrder adds items
    QString orderByClause() const override;                  // "created_at DESC"
};

// IngredientRepository.hpp  — tables "ingredients" + "inventory_transactions"
class IngredientRepository : public Repository<models::Ingredient> {
public:
    IngredientRepository();
    int insert(const models::Ingredient& i);
    void update(const models::Ingredient& i);
    void adjustStock(int ingredientId, double deltaQty, const QString& reason,
                     int refOrderId = 0, core::Money unitCost = core::Money(),
                     const QString& note = QString());       // one transaction: UPDATE stock + INSERT txn row
    std::vector<models::Ingredient> lowStock() const;
    std::vector<models::Ingredient> expiringWithin(int days) const;
    // (ts, delta, reason, note) newest first:
    std::vector<std::tuple<QDateTime, double, QString, QString>> history(int ingredientId, int limit = 50) const;
protected:
    models::Ingredient fromRecord(const QSqlRecord& rec) const override;
    QString orderByClause() const override;                  // "name"
};

// SupplierRepository.hpp    — table "suppliers"
class SupplierRepository : public Repository<models::Supplier> {
public:
    SupplierRepository();
    int insert(const models::Supplier& s);
    void update(const models::Supplier& s);
protected:
    models::Supplier fromRecord(const QSqlRecord& rec) const override;
    QString orderByClause() const override;                  // "name"
};

// TableRepository.hpp       — table "tables"
class TableRepository : public Repository<models::Table> {
public:
    TableRepository();
    int insert(const models::Table& t);
    void update(const models::Table& t);
    std::vector<models::Table> activeWithCapacityAtLeast(int guests) const;
protected:
    models::Table fromRecord(const QSqlRecord& rec) const override;
    QString orderByClause() const override;                  // "name"
};

// ReservationRepository.hpp — table "reservations"
class ReservationRepository : public Repository<models::Reservation> {
public:
    ReservationRepository();
    int insert(const models::Reservation& r);
    void update(const models::Reservation& r);
    void setStatus(int reservationId, models::ReservationStatus s);
    std::vector<models::Reservation> onDay(QDate day) const;
    std::vector<models::Reservation> overlapping(int tableId, const QDateTime& start, int durationMin) const;
        // status IN (Booked, Seated) only
protected:
    models::Reservation fromRecord(const QSqlRecord& rec) const override;
    QString orderByClause() const override;                  // "starts_at"
};

// PaymentRepository.hpp     — table "payments"
class PaymentRepository : public Repository<models::Payment> {
public:
    PaymentRepository();
    int insert(const models::Payment& p);
    std::vector<models::Payment> between(const QDateTime& from, const QDateTime& to) const;
    core::Money revenueBetween(const QDateTime& from, const QDateTime& to) const;   // SUM(total_paisa)
    // (menu item name, qty sold) top-N by qty within window:
    std::vector<std::pair<QString, int>> popularItems(const QDateTime& from, const QDateTime& to, int topN) const;
protected:
    models::Payment fromRecord(const QSqlRecord& rec) const override;
    QString orderByClause() const override;                  // "paid_at DESC"
};

// PromoRepository.hpp       — table "promos"
class PromoRepository : public Repository<models::Promo> {
public:
    PromoRepository();
    int insert(const models::Promo& p);
    void update(const models::Promo& p);
    std::optional<models::Promo> byCode(const QString& code) const;   // case-insensitive
protected:
    models::Promo fromRecord(const QSqlRecord& rec) const override;
    QString orderByClause() const override;                  // "code"
};

// SettingsRepository.hpp    — table "settings" (no Repository<T> base: not id-keyed)
class SettingsRepository {
public:
    SettingsRepository() = default;
    QString get(const QString& key, const QString& fallback = QString()) const;
    void set(const QString& key, const QString& value);      // UPSERT
    void remove(const QString& key);
};

// AuditRepository.hpp       — table "audit_log" (SQLite mirror of the binary trail)
class AuditRepository {
public:
    AuditRepository() = default;
    void insert(quint32 seq, qint64 tsUtcMs, int userId, const QString& action,
                const QString& entity, core::Money amount, const QString& details);
    // (seq, ts, userId, action, entity, amount, details) newest first:
    std::vector<std::tuple<quint32, QDateTime, int, QString, QString, core::Money, QString>>
        recent(int limit = 100) const;
};
```

#### `persistence/BinaryRecordFile.hpp` — the raw random-access binary layer

```cpp
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <type_traits>
#include <QString>

namespace aluchop::persistence {

/// Fixed 128-byte on-disk record. Field order is chosen so there is ZERO padding.
/// @oop-concept Structures / Binary File Handling :: trivially-copyable POD written with raw read/write
struct AuditRecord {
    std::int64_t  timestampUtcMs;   // offset   0, 8 bytes — ms since Unix epoch, UTC
    std::int64_t  amountPaisa;      // offset   8, 8 bytes — money involved (0 if none)
    std::uint32_t magic;            // offset  16, 4 bytes — 0x414C4348 ('A''L''C''H')
    std::uint32_t seq;              // offset  20, 4 bytes — 1-based monotone sequence
    std::uint32_t userId;           // offset  24, 4 bytes — acting user, 0 = system
    char          action[16];       // offset  28 — NUL-padded ASCII, e.g. "ORDER_PAID"
    char          entity[16];       // offset  44 — e.g. "order:42"
    char          details[64];      // offset  60 — free text, truncated
    std::uint32_t checksum;         // offset 124 — byte-wise additive checksum of offsets [0,124)
};                                  // total 128 bytes
static_assert(sizeof(AuditRecord) == 128, "AuditRecord must be exactly 128 bytes");
static_assert(std::is_trivially_copyable_v<AuditRecord>, "AuditRecord must be a POD");

class BinaryRecordFile {
public:
    explicit BinaryRecordFile(QString path);
    ~BinaryRecordFile();                             // closes stream (RAII)
    BinaryRecordFile(const BinaryRecordFile&) = delete;
    BinaryRecordFile& operator=(const BinaryRecordFile&) = delete;

    void openOrCreate();                             // creates empty file if missing; opens in|out|binary; throws FileIOException
    void close();
    bool isOpen() const;

    std::size_t recordCount();                       // seekg(0, end); tellg()/128; throws on stream failure

    /// @oop-concept File Handling (Write/Append) :: seekp(0, end) + raw write + flush, every step checked
    void append(const AuditRecord& rec);

    /// @oop-concept Random Access File IO :: seekg(index * sizeof(AuditRecord)) direct positioning
    AuditRecord readAt(std::size_t index);           // verifies magic + checksum; throws FileIOException on mismatch
    void overwriteAt(std::size_t index, const AuditRecord& rec);   // seekp random-access write

    static std::uint32_t checksumOf(const AuditRecord& rec);       // sums bytes [0,124)
    static void fillString(char* dest, std::size_t cap, const QString& src);  // NUL-padded ASCII copy

protected:
    void ensureOpen();                               // throws FileIOException("audit file not open")
    std::fstream m_stream;
    QString m_path;
};

} // namespace aluchop::persistence
```

Error-checking contract (binding): after **every** `seekg/seekp/read/write/flush`, test the stream
(`if (!m_stream) throw core::FileIOException(...)`), and `m_stream.clear()` before reuse after EOF probes.

#### `persistence/AuditTrail.hpp`

```cpp
#include <vector>
#include "aluchop/persistence/BinaryRecordFile.hpp"
#include "aluchop/core/Money.hpp"

namespace aluchop::persistence {

/// @oop-concept Private Inheritance :: an AuditTrail is IMPLEMENTED-IN-TERMS-OF a record file,
/// it is not one — private inheritance stops callers bypassing sequencing/checksum invariants.
class AuditTrail : private BinaryRecordFile {
public:
    explicit AuditTrail(const QString& path);        // openOrCreate(); m_nextSeq = recordCount()+1
    std::uint32_t record(std::uint32_t userId, const QString& action, const QString& entity,
                         core::Money amount, const QString& details);   // returns seq written
    AuditRecord at(std::size_t index);               // random access, re-exposed
    std::vector<AuditRecord> tail(std::size_t n);    // last n records via readAt, oldest→newest
    std::size_t size();                              // recordCount()
    bool verifyIntegrity(std::size_t& firstBadIndex);   // full checksum walk; true = clean

    using BinaryRecordFile::close;                   // selectively re-exposed, nothing else

private:
    std::uint32_t m_nextSeq = 1;
};

} // namespace aluchop::persistence
```

#### `persistence/CsvWriter.hpp`

```cpp
#include <fstream>
#include <QString>
#include <QStringList>

namespace aluchop::persistence {

/// @oop-concept File Handling (Sequential ASCII) :: plain std::ofstream CSV with per-write checks
class CsvWriter {
public:
    CsvWriter() = default;
    ~CsvWriter();                                    // closes if open (RAII)
    CsvWriter(const CsvWriter&) = delete;
    CsvWriter& operator=(const CsvWriter&) = delete;

    void open(const QString& path);                  // std::ofstream trunc mode; creates dirs; throws FileIOException
    void writeRow(const QStringList& cells);         // escapes, writes, checks m_out.good() — throws on failure
    void close();                                    // flush + final state check; throws on failure
    bool isOpen() const;
    int rowsWritten() const noexcept { return m_rows; }

    static QString escapeCell(const QString& cell);  // RFC-4180 quoting

protected:                                           // protected so ReportGenerator's children may drive it
    std::ofstream m_out;
    QString m_path;
    int m_rows = 0;
};

} // namespace aluchop::persistence
```

#### `persistence/BackupManager.hpp`

```cpp
#include <vector>
#include <QString>

namespace aluchop::persistence {

class BackupManager {
public:
    BackupManager(QString dbPath, QString backupDir);
    QString createBackup();                          // closes nothing (SQLite file copy is safe after wal_checkpoint);
                                                     // writes "<backupDir>/aluchop-YYYYMMDD-HHmmss.db"; throws FileIOException
    void restoreBackup(const QString& backupFile);   // validate header → close Database → swap file → reopen; throws
    std::vector<QString> listBackups() const;        // newest first
    static bool isValidSqliteFile(const QString& path);
        // std::ifstream binary read of first 16 bytes == "SQLite format 3\0" — error-checked
private:
    QString m_dbPath, m_backupDir;
};

} // namespace aluchop::persistence
```

### 3.4 `aluchop::services`

Constructor parameters below are the **exact wiring** `AppContext` performs. Services hold the
references they receive; they never own repositories.

#### `services/NotificationService.hpp` (the only QObject service)

```cpp
#include <QObject>
#include <QString>

namespace aluchop::services {

/// Central event bus: services announce, GUI listens. Keeps every other service moc-free.
class NotificationService : public QObject {
    Q_OBJECT
public:
    explicit NotificationService(QObject* parent = nullptr);
    void notify(const QString& title, const QString& message, int level = 0);
        // level: 0 Info, 1 Success, 2 Warning, 3 Danger (models::NoticeLevel as int for the signal)
    void announceDataChanged(const QString& domain);
        // domains (exact strings): "menu" "orders" "customers" "employees" "inventory"
        //                          "reservations" "payments" "settings"
signals:
    void notification(const QString& title, const QString& message, int level);
    void dataChanged(const QString& domain);
};

} // namespace aluchop::services
```

#### `services/AuditService.hpp`

```cpp
#include "aluchop/persistence/AuditTrail.hpp"
#include "aluchop/persistence/AuditRepository.hpp"
#include "aluchop/core/Money.hpp"

namespace aluchop::services {

class AuditService {
public:
    AuditService(persistence::AuditTrail& trail, persistence::AuditRepository& mirror);
    void setActiveUser(int userId);                  // AuthService calls on login/logout(0)
    /// @oop-concept Function Overloading + Default Arguments :: light and full logging forms
    void log(const QString& action, const QString& entity);
    void log(const QString& action, const QString& entity,
             core::Money amount, const QString& details = QString());
        // writes binary trail first, then SQLite mirror; a FileIOException here is
        // logged via core::Logger and RETHROWN (`throw;`) — audit must never fail silently
    // --- GUI-lawful surface over the binary trail (see §12, reconciliation R1) ---
    // ReportsPage::onVerifyAudit() calls these. It must NOT reach through trail(),
    // because naming persistence::AuditTrail forces a gui TU to include a persistence
    // header and breaks the layer rule.
    bool verifyTrailIntegrity(std::size_t& firstBadIndex);   // delegates to AuditTrail::verifyIntegrity
    std::size_t trailRecordCount();                          // delegates to AuditTrail::size
    persistence::AuditRecord trailRecordAt(std::size_t index);          // random-access proof
    std::vector<persistence::AuditRecord> recentTrailRecords(std::size_t n);

    persistence::AuditTrail& trail() { return m_trail; }     // services-layer/tests ONLY, never gui
private:
    persistence::AuditTrail& m_trail;
    persistence::AuditRepository& m_mirror;
    int m_activeUserId = 0;
};

} // namespace aluchop::services
```

#### `services/AuthService.hpp`

```cpp
#include <optional>
#include "aluchop/core/Result.hpp"
#include "aluchop/models/User.hpp"
#include "aluchop/persistence/UserRepository.hpp"
#include "aluchop/persistence/SettingsRepository.hpp"

namespace aluchop::services {
class AuditService;

class AuthService {
public:
    AuthService(persistence::UserRepository& users,
                persistence::SettingsRepository& settings,
                AuditService& audit);

    core::Result<models::User> login(const QString& username, const QString& password,
                                     bool rememberMe = false);
    void logout();
    const std::optional<models::User>& currentUser() const noexcept { return m_current; }
    bool isLoggedIn() const noexcept { return m_current.has_value(); }
    bool hasRole(models::UserRole atLeast) const;    // Admin > Manager > Waiter == Chef (staff tier)

    std::optional<models::User> tryRememberedLogin();    // reads settings "remember_token"
    core::Result<void> changePassword(const QString& oldPassword, const QString& newPassword);
    core::Result<QString> securityQuestionFor(const QString& username);
    core::Result<void> resetPasswordWithAnswer(const QString& username,
                                               const QString& answer, const QString& newPassword);
    core::Result<int> createUser(const QString& username, const QString& password,
                                 models::UserRole role, int employeeId,
                                 const QString& securityQuestion, const QString& securityAnswer);
        // Admin-only; enforced here, not just in GUI

    static QString hashPassword(const QString& password, const QString& salt);  // SHA-256(salt+password), hex
    static QString generateSalt();                   // 16 random bytes, hex (QRandomGenerator::system())
    static const int kMinPasswordLength = 6;

private:
    persistence::UserRepository& m_users;
    persistence::SettingsRepository& m_settings;
    AuditService& m_audit;
    std::optional<models::User> m_current;
};

} // namespace aluchop::services
```

Internally `login` throws/catches `core::AuthException` for bad credentials and converts to
`Result::err` — GUI never sees exceptions.

#### `services/MenuService.hpp`

```cpp
#include <vector>
#include "aluchop/core/Result.hpp"
#include "aluchop/models/MenuItem.hpp"
#include "aluchop/models/RecipeLine.hpp"
#include "aluchop/persistence/MenuRepository.hpp"

namespace aluchop::services {
class AuditService; class NotificationService;

enum class MenuSort { NameAsc, NameDesc, PriceAsc, PriceDesc, Category };

class MenuService {
public:
    MenuService(persistence::MenuRepository& repo, AuditService& audit, NotificationService& notify);

    std::vector<models::MenuItem> all() const;
    /// @oop-concept Default Arguments :: one search entry point serves the whole menu page
    std::vector<models::MenuItem> search(const QString& term,
                                         const QString& category = QString(),
                                         bool availableOnly = false,
                                         MenuSort sort = MenuSort::NameAsc) const;
    std::vector<QString> categories() const;         // from core::kMenuCategories, in order
    core::Result<int> create(const models::MenuItem& item);
    core::Result<void> update(const models::MenuItem& item);
    core::Result<void> remove(int itemId);           // err if referenced by open orders
    core::Result<void> setAvailability(int itemId, bool available);
    std::vector<models::RecipeLine> recipeFor(int menuItemId) const;
    core::Result<void> setRecipe(int menuItemId, const std::vector<models::RecipeLine>& lines);

private:
    persistence::MenuRepository& m_repo;
    AuditService& m_audit;
    NotificationService& m_notify;
};

} // namespace aluchop::services
```

`search` sorts with `std::sort` + comparators (uses `MenuItem::operator<` for NameAsc).

#### `services/KitchenQueue.hpp` (header-only)

```cpp
#include <queue>
#include <vector>
#include "aluchop/core/Exceptions.hpp"

namespace aluchop::services {

/// @oop-concept STL (queue) :: FIFO of order ids waiting for the kitchen
class KitchenQueue {
public:
    void push(int orderId) { m_q.push(orderId); }
    int front() const {
        if (m_q.empty()) throw core::ValidationException("kitchen queue is empty");
        return m_q.front();
    }
    void pop() {
        if (m_q.empty()) throw core::ValidationException("kitchen queue is empty");
        m_q.pop();
    }
    bool empty() const noexcept { return m_q.empty(); }
    std::size_t size() const noexcept { return m_q.size(); }
    std::vector<int> snapshot() const;               // copies queue without draining (copy + drain the copy)
    void remove(int orderId);                        // rebuilds queue without orderId (cancel path)
private:
    std::queue<int> m_q;
};

} // namespace aluchop::services
```

#### `services/OrderService.hpp`

```cpp
#include <vector>
#include "aluchop/core/Result.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/persistence/OrderRepository.hpp"
#include "aluchop/persistence/MenuRepository.hpp"
#include "aluchop/persistence/TableRepository.hpp"
#include "aluchop/services/KitchenQueue.hpp"

namespace aluchop::services {
class InventoryService; class CustomerService; class AuditService; class NotificationService;

class OrderService {
public:
    OrderService(persistence::OrderRepository& orders, persistence::MenuRepository& menu,
                 persistence::TableRepository& tables, InventoryService& inventory,
                 CustomerService& customers, AuditService& audit, NotificationService& notify);

    core::Result<models::Order> createOrder(models::OrderType type, int tableId = 0,
                                            int customerId = 0, int waiterId = 0);
        // DineIn requires an active table; on startup AppContext calls rebuildQueue()
    core::Result<void> addItem(int orderId, int menuItemId, int qty);        // item must be available
    core::Result<void> updateItemQty(int orderId, std::size_t index, int qty);
    core::Result<void> removeItem(int orderId, std::size_t index);
    core::Result<void> setOrderNote(int orderId, const QString& note);
    core::Result<void> cancelOrder(int orderId);

    /// Split: copies the order (Order copy ctor), moves the chosen line indexes into the copy,
    /// persists the copy as a new order, rewrites the original. Both stay in the same status.
    core::Result<models::Order> splitOrder(int orderId, const std::vector<std::size_t>& itemIndexes);
    /// Merge: target += source (Order::operator+=), source marked Cancelled + merged_into set.
    core::Result<void> mergeOrders(int targetOrderId, int sourceOrderId);

    core::Result<void> submitToKitchen(int orderId);         // Open → Pending, pushes on queue
    core::Result<void> advanceStatus(int orderId);
        // Pending→Preparing→Ready→Served. On Served: inventory.deductForOrder(order) and, if the
        // order has a customer, customers.recordVisit(...). Catches InventoryException separately
        // from DatabaseException (multiple catch) — inventory shortfall notifies but does not
        // block serving; DB failure is returned as err.

    std::optional<models::Order> order(int orderId) const;
    std::vector<models::Order> activeOrders() const;
    std::vector<models::Order> withStatus(models::OrderStatus s) const;
    KitchenQueue& kitchenQueue() noexcept { return m_queue; }    // return by reference
    void rebuildQueue();                                          // from DB Pending orders, oldest first

private:
    persistence::OrderRepository& m_orders;
    persistence::MenuRepository& m_menu;
    persistence::TableRepository& m_tables;
    InventoryService& m_inventory;
    CustomerService& m_customers;
    AuditService& m_audit;
    NotificationService& m_notify;
    KitchenQueue m_queue;                            // object as member
};

} // namespace aluchop::services
```

#### `services/BillingService.hpp`

```cpp
#include "aluchop/core/Result.hpp"
#include "aluchop/models/Bill.hpp"
#include "aluchop/models/Payment.hpp"
#include "aluchop/persistence/OrderRepository.hpp"
#include "aluchop/persistence/PaymentRepository.hpp"
#include "aluchop/persistence/PromoRepository.hpp"

namespace aluchop::services {
class CustomerService; class EmployeeService; class AuditService; class NotificationService;

class BillingService {
public:
    BillingService(persistence::OrderRepository& orders, persistence::PaymentRepository& payments,
                   persistence::PromoRepository& promos, CustomerService& customers,
                   EmployeeService& employees, AuditService& audit, NotificationService& notify);

    /// Builds the bill for a Served order. Discount rule (binding, never stacked):
    ///   candidates = { promo discount if code valid, staff discount if the order's customer is
    ///                  an active employee (EmployeeService::staffCustomerFor), manual 0 }
    ///   → apply the single LARGEST. Service charge = subtotal.percent(serviceChargePercent),
    ///   added AFTER discount. Prices are tax-inclusive; nothing else is ever added.
    core::Result<models::Bill> prepareBill(int orderId,
                                           const QString& promoCode = QString(),
                                           int serviceChargePercent = 0);

    /// Cash: tendered >= total else ValidationException→err; change = tendered − total.
    /// Card/Wallet: tendered forced = total, change = 0.
    /// One DB transaction: insert payment, order → Paid, loyalty award (total/100 rupees = points).
    core::Result<models::Payment> settle(int orderId, models::Bill& bill,
                                         models::PaymentMethod method, core::Money tendered,
                                         int cashierUserId);

    core::Money changeFor(core::Money total, core::Money tendered) const;
        // throws core::ValidationException("insufficient tender") if tendered < total
    QString receiptText(const models::Bill& bill) const;     // via Bill::toPrintableText()

private:
    persistence::OrderRepository& m_orders;
    persistence::PaymentRepository& m_payments;
    persistence::PromoRepository& m_promos;
    CustomerService& m_customers;
    EmployeeService& m_employees;
    AuditService& m_audit;
    NotificationService& m_notify;
};

} // namespace aluchop::services
```

#### `services/CustomerService.hpp`

```cpp
#include <vector>
#include "aluchop/core/Result.hpp"
#include "aluchop/models/Customer.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/persistence/CustomerRepository.hpp"
#include "aluchop/persistence/OrderRepository.hpp"

namespace aluchop::services {
class AuditService; class NotificationService;

class CustomerService {
public:
    CustomerService(persistence::CustomerRepository& customers,
                    persistence::OrderRepository& orders,
                    AuditService& audit, NotificationService& notify);

    std::vector<models::Customer> all() const;
    std::vector<models::Customer> search(const QString& term) const;
    std::optional<models::Customer> byId(int id) const;
    std::optional<models::Customer> byPhone(const QString& phone) const;
    core::Result<int> create(const QString& name, const QString& phone, const QString& email);
    core::Result<void> update(const models::Customer& c);
    core::Result<void> remove(int customerId);

    /// Uses ++customer (prefix increment) then awards points: spent.wholeRupees()/100.
    core::Result<void> recordVisit(int customerId, core::Money spent);
    core::Result<void> redeemPoints(int customerId, int points);
    std::vector<models::Order> visitHistory(int customerId, int limit = 20) const;
    std::vector<QString> favouriteItems(int customerId, int topN = 3) const;   // by qty across history

private:
    persistence::CustomerRepository& m_customers;
    persistence::OrderRepository& m_orders;
    AuditService& m_audit;
    NotificationService& m_notify;
};

} // namespace aluchop::services
```

#### `services/EmployeeService.hpp`

```cpp
#include <memory>
#include <vector>
#include <optional>
#include "aluchop/core/Result.hpp"
#include "aluchop/models/Employee.hpp"
#include "aluchop/models/StaffCustomer.hpp"
#include "aluchop/persistence/EmployeeRepository.hpp"
#include "aluchop/persistence/CustomerRepository.hpp"

namespace aluchop::services {
class AuditService; class NotificationService;

class EmployeeService {
public:
    EmployeeService(persistence::EmployeeRepository& employees,
                    persistence::CustomerRepository& customers,
                    AuditService& audit, NotificationService& notify);

    /// @oop-concept Object Pointers / Runtime Polymorphism :: payroll iterates unique_ptr<Employee>
    /// and calls monthlyPay() virtually — Waiter/Chef/Manager/Admin each compute differently
    std::vector<std::unique_ptr<models::Employee>> staff() const;        // via repo.allTyped()
    std::optional<models::Employee> byId(int id) const;                  // base slice for forms
    core::Result<int> hire(const QString& name, const QString& phone, const QString& email,
                           const QString& position, core::Money salary, const QString& shift);
    core::Result<void> update(const models::Employee& e);
    core::Result<void> deactivate(int employeeId);

    core::Result<void> markAttendance(int employeeId, QDate day, const QString& status,
                                      QTime checkIn = QTime(), QTime checkOut = QTime());
    std::vector<std::tuple<QDate, QString, QTime, QTime>>
        attendanceFor(int employeeId, int year, int month) const;

    // (displayLabel, roleName, monthlyPay) for every active staff member — virtual dispatch:
    std::vector<std::tuple<QString, QString, core::Money>> payrollPreview() const;

    /// The diamond in action: if this customer's phone matches an active employee,
    /// returns the fused StaffCustomer (one Person identity) for staff-discount billing.
    std::optional<models::StaffCustomer> staffCustomerFor(int customerId) const;

private:
    persistence::EmployeeRepository& m_employees;
    persistence::CustomerRepository& m_customers;
    AuditService& m_audit;
    NotificationService& m_notify;
};

} // namespace aluchop::services
```

#### `services/InventoryService.hpp`

```cpp
#include <vector>
#include "aluchop/core/Result.hpp"
#include "aluchop/models/Ingredient.hpp"
#include "aluchop/models/Supplier.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/persistence/IngredientRepository.hpp"
#include "aluchop/persistence/SupplierRepository.hpp"
#include "aluchop/persistence/MenuRepository.hpp"

namespace aluchop::services {
class AuditService; class NotificationService;

class InventoryService {
public:
    InventoryService(persistence::IngredientRepository& ingredients,
                     persistence::SupplierRepository& suppliers,
                     persistence::MenuRepository& menu,
                     AuditService& audit, NotificationService& notify);

    std::vector<models::Ingredient> all() const;
    core::Result<int> addIngredient(const models::Ingredient& i);
    core::Result<void> updateIngredient(const models::Ingredient& i);
    core::Result<void> restock(int ingredientId, double qty, core::Money unitCost,
                               const QString& note = QString());   // qty must be > 0
    core::Result<void> recordWaste(int ingredientId, double qty, const QString& note);

    /// Called by OrderService when an order is Served. For each line: recipe × qty deducted
    /// (reason "USAGE", ref order id). Missing ingredient row → throws core::InventoryException.
    /// Stock may go below threshold (notifies "Low stock") but is clamped at 0 with a Warning.
    void deductForOrder(const models::Order& order);

    std::vector<models::Ingredient> lowStock() const;
    std::vector<models::Ingredient> expiring(int days = 7) const;   // default argument
    std::vector<std::tuple<QDateTime, double, QString, QString>>
        history(int ingredientId, int limit = 50) const;

    std::vector<models::Supplier> suppliers() const;
    core::Result<int> addSupplier(const models::Supplier& s);
    core::Result<void> updateSupplier(const models::Supplier& s);

private:
    persistence::IngredientRepository& m_ingredients;
    persistence::SupplierRepository& m_suppliers;
    persistence::MenuRepository& m_menu;
    AuditService& m_audit;
    NotificationService& m_notify;
};

} // namespace aluchop::services
```

#### `services/ReservationService.hpp`

```cpp
#include <vector>
#include "aluchop/core/Result.hpp"
#include "aluchop/models/Reservation.hpp"
#include "aluchop/models/Table.hpp"
#include "aluchop/persistence/ReservationRepository.hpp"
#include "aluchop/persistence/TableRepository.hpp"
#include "aluchop/persistence/CustomerRepository.hpp"

namespace aluchop::services {
class AuditService; class NotificationService;

class ReservationService {
public:
    ReservationService(persistence::ReservationRepository& reservations,
                       persistence::TableRepository& tables,
                       persistence::CustomerRepository& customers,
                       AuditService& audit, NotificationService& notify);

    std::vector<models::Table> availableTables(const QDateTime& start, int durationMin, int guests) const;
        // active tables with capacity >= guests minus overlapping Booked/Seated reservations
    core::Result<int> book(const models::Reservation& r);    // err if table unavailable / past time
    core::Result<void> update(const models::Reservation& r);
    core::Result<void> cancel(int reservationId);
    core::Result<void> seat(int reservationId);              // Booked → Seated
    core::Result<void> complete(int reservationId);          // Seated → Completed
    std::vector<models::Reservation> onDay(QDate day) const;
    std::vector<models::Table> tables() const;

private:
    persistence::ReservationRepository& m_reservations;
    persistence::TableRepository& m_tables;
    persistence::CustomerRepository& m_customers;
    AuditService& m_audit;
    NotificationService& m_notify;
};

} // namespace aluchop::services
```

#### `services/ReportGenerator.hpp` — protected inheritance lives here

```cpp
#include <vector>
#include <QString>
#include <QStringList>
#include <QDate>
#include "aluchop/persistence/CsvWriter.hpp"
#include "aluchop/persistence/PaymentRepository.hpp"
#include "aluchop/persistence/OrderRepository.hpp"
#include "aluchop/persistence/CustomerRepository.hpp"
#include "aluchop/persistence/EmployeeRepository.hpp"
#include "aluchop/persistence/IngredientRepository.hpp"

namespace aluchop::services {

/// @oop-concept Protected Inheritance :: a report is implemented-in-terms-of a CsvWriter and its
/// DERIVED report classes also need the writer verbs — protected (not private) re-use, while
/// outside callers can only exportCsv()/header()/rows(), never writeRow() directly.
class ReportGenerator : protected persistence::CsvWriter {
public:
    virtual ~ReportGenerator() = default;            // ReportGenerator introduces the virtual dtor;
                                                     // the protected CsvWriter base is never deleted through
    virtual QString title() const = 0;
    virtual QStringList header() const = 0;
    virtual std::vector<QStringList> rows() const = 0;   // computed fresh on each call
    QString exportCsv(const QString& outPath);       // template method: open → header → rows → close; returns path
};

class SalesReport final : public ReportGenerator {
public:
    SalesReport(const persistence::PaymentRepository& payments, QDate from, QDate to);
    QString title() const override;                  // "Sales Report <from>–<to>"
    QStringList header() const override;             // Date | Orders Paid | Revenue
    std::vector<QStringList> rows() const override;
private:
    const persistence::PaymentRepository& m_payments;
    QDate m_from, m_to;
};

class InventoryReport final : public ReportGenerator {
public:
    explicit InventoryReport(const persistence::IngredientRepository& ingredients);
    QString title() const override;
    QStringList header() const override;             // Ingredient | Unit | Stock | Threshold | Low? | Expiry | Unit Cost
    std::vector<QStringList> rows() const override;
private:
    const persistence::IngredientRepository& m_ingredients;
};

class OrdersReport final : public ReportGenerator {
public:
    OrdersReport(const persistence::OrderRepository& orders, QDate from, QDate to);
    QString title() const override;
    QStringList header() const override;             // Order # | Type | Status | Items | Subtotal | Created
    std::vector<QStringList> rows() const override;
private:
    const persistence::OrderRepository& m_orders;
    QDate m_from, m_to;
};

class CustomersReport final : public ReportGenerator {
public:
    explicit CustomersReport(const persistence::CustomerRepository& customers);
    QString title() const override;
    QStringList header() const override;             // Name | Phone | Email | Visits | Points
    std::vector<QStringList> rows() const override;
private:
    const persistence::CustomerRepository& m_customers;
};

class EmployeesReport final : public ReportGenerator {
public:
    explicit EmployeesReport(const persistence::EmployeeRepository& employees);
    QString title() const override;
    QStringList header() const override;             // Name | Position | Shift | Salary | Monthly Pay | Rating
    std::vector<QStringList> rows() const override;  // Monthly Pay via makeTyped() → virtual monthlyPay()
private:
    const persistence::EmployeeRepository& m_employees;
};

} // namespace aluchop::services
```

#### `services/ReportService.hpp`

```cpp
#include <array>
#include <memory>
#include <vector>
#include "aluchop/services/ReportGenerator.hpp"
#include "aluchop/core/Money.hpp"

namespace aluchop::services {

enum class ReportKind { Sales, Inventory, Orders, Customers, Employees };

class ReportService {
public:
    ReportService(const persistence::PaymentRepository& payments,
                  const persistence::OrderRepository& orders,
                  const persistence::CustomerRepository& customers,
                  const persistence::EmployeeRepository& employees,
                  const persistence::IngredientRepository& ingredients);

    // Dashboard aggregates:
    core::Money salesForDay(QDate day) const;
    /// @oop-concept Object Arrays :: seven Money objects, one per day, drive the weekly chart
    std::array<core::Money, 7> weeklySales(QDate weekEnding) const;      // [0]=6 days ago … [6]=weekEnding
    core::Money salesForMonth(int year, int month) const;
    std::vector<std::pair<QString, int>> popularItems(int topN = 5) const;   // last 30 days
    std::vector<std::pair<QDate, core::Money>> revenueSeries(QDate from, QDate to) const;
    int customerCount() const;
    int pendingOrderCount() const;                   // status IN (Pending, Preparing, Ready)

    /// @oop-concept Dynamic Objects :: reports are created on the heap and owned by unique_ptr
    std::unique_ptr<ReportGenerator> makeReport(ReportKind kind, QDate from, QDate to) const;

private:
    const persistence::PaymentRepository& m_payments;
    const persistence::OrderRepository& m_orders;
    const persistence::CustomerRepository& m_customers;
    const persistence::EmployeeRepository& m_employees;
    const persistence::IngredientRepository& m_ingredients;
};

} // namespace aluchop::services
```

#### `services/SettingsService.hpp`

```cpp
#include <map>
#include "aluchop/core/Result.hpp"
#include "aluchop/persistence/SettingsRepository.hpp"
#include "aluchop/persistence/BackupManager.hpp"

namespace aluchop::services {
class AuditService;

class SettingsService {
public:
    SettingsService(persistence::SettingsRepository& repo,
                    persistence::BackupManager& backups, AuditService& audit);

    QString get(const QString& key, const QString& fallback = QString()) const;   // cached
    void set(const QString& key, const QString& value);      // writes DB + cache
    // Well-known keys: "restaurant.name" "restaurant.address" "restaurant.phone"
    //                  "theme.mode" ("light"/"dark")  "billing.service_charge_pct"
    core::Result<QString> createBackup();                    // returns backup path
    core::Result<void> restoreBackup(const QString& path);
    std::vector<QString> listBackups() const;

private:
    persistence::SettingsRepository& m_repo;
    persistence::BackupManager& m_backups;
    AuditService& m_audit;
    mutable std::map<QString, QString> m_cache;              // STL map — mutable for const get()
};

} // namespace aluchop::services
```

#### `services/Commands.hpp` — undo/redo

```cpp
#include <memory>
#include <vector>
#include <QString>
#include "aluchop/core/Money.hpp"

namespace aluchop::services {
class OrderService; class MenuService; class InventoryService;

/// @oop-concept Pure Virtual Functions / Runtime Polymorphism :: every undoable edit is a Command
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;                      // throws AluChopException subtypes on failure
    virtual void undo() = 0;
    virtual QString description() const = 0;         // "Add 2 × Momo to ORD-…"
};

class AddOrderItemCommand final : public Command {
public:
    AddOrderItemCommand(OrderService& svc, int orderId, int menuItemId, int qty);
    void execute() override;  void undo() override;  QString description() const override;
private:
    OrderService& m_svc; int m_orderId, m_menuItemId, m_qty;
    std::size_t m_addedIndex = 0;
};

class RemoveOrderItemCommand final : public Command {
public:
    RemoveOrderItemCommand(OrderService& svc, int orderId, std::size_t index);
    void execute() override;  void undo() override;  QString description() const override;
private:
    OrderService& m_svc; int m_orderId; std::size_t m_index;
    int m_menuItemId = 0, m_qty = 0;                 // captured at execute() for undo
};

class ToggleAvailabilityCommand final : public Command {
public:
    ToggleAvailabilityCommand(MenuService& svc, int itemId, bool makeAvailable);
    void execute() override;  void undo() override;  QString description() const override;
private:
    MenuService& m_svc; int m_itemId; bool m_makeAvailable;
};

class AdjustStockCommand final : public Command {
public:
    AdjustStockCommand(InventoryService& svc, int ingredientId, double qty,
                       core::Money unitCost, QString note);
    void execute() override;  void undo() override;  QString description() const override;
private:
    InventoryService& m_svc; int m_ingredientId; double m_qty;
    core::Money m_unitCost; QString m_note;
};

class CommandStack {
public:
    /// executes; on success pushes to undo stack and clears redo stack
    core::Result<void> run(std::unique_ptr<Command> cmd);
    bool canUndo() const noexcept { return !m_undo.empty(); }
    bool canRedo() const noexcept { return !m_redo.empty(); }
    QString undoText() const;                        // "" when empty
    QString redoText() const;
    core::Result<void> undo();
    core::Result<void> redo();
    static constexpr std::size_t kMaxDepth = 50;
private:
    std::vector<std::unique_ptr<Command>> m_undo, m_redo;    // STL vector of object pointers
};

} // namespace aluchop::services
```

#### `services/AppContext.hpp` — composition root

```cpp
#include "aluchop/persistence/Database.hpp"
#include "aluchop/persistence/SchemaMigrator.hpp"
// ... all repository headers, AuditTrail, BackupManager, all service headers ...

namespace aluchop::services {

/// @oop-concept Objects as Members :: the whole object graph is value members, wired by reference.
/// Declaration order below IS initialisation order — do not reorder.
class AppContext {
public:
    /// Opens DB at <dataDir>/aluchop.db, migrates+seeds, opens audit file <dataDir>/audit.bin,
    /// then constructs every repo and service. Throws DatabaseException / FileIOException.
    explicit AppContext(const QString& dataDir, const QString& menuSeedJsonPath);
    ~AppContext() = default;
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    AuthService&         auth()          noexcept { return m_auth; }
    MenuService&         menu()          noexcept { return m_menuSvc; }
    OrderService&        orders()        noexcept { return m_orderSvc; }
    BillingService&      billing()       noexcept { return m_billingSvc; }
    CustomerService&     customers()     noexcept { return m_customerSvc; }
    EmployeeService&     employees()     noexcept { return m_employeeSvc; }
    InventoryService&    inventory()     noexcept { return m_inventorySvc; }
    ReservationService&  reservations()  noexcept { return m_reservationSvc; }
    ReportService&       reports()       noexcept { return m_reportSvc; }
    SettingsService&     settings()      noexcept { return m_settingsSvc; }
    AuditService&        audit()         noexcept { return m_auditSvc; }
    NotificationService& notifications() noexcept { return m_notify; }
    CommandStack&        commands()      noexcept { return m_commands; }

private:
    // repositories (values):
    persistence::UserRepository        m_userRepo;
    persistence::MenuRepository        m_menuRepo;
    persistence::CustomerRepository    m_customerRepo;
    persistence::EmployeeRepository    m_employeeRepo;
    persistence::OrderRepository       m_orderRepo;
    persistence::IngredientRepository  m_ingredientRepo;
    persistence::SupplierRepository    m_supplierRepo;
    persistence::TableRepository       m_tableRepo;
    persistence::ReservationRepository m_reservationRepo;
    persistence::PaymentRepository     m_paymentRepo;
    persistence::PromoRepository       m_promoRepo;
    persistence::SettingsRepository    m_settingsRepo;
    persistence::AuditRepository       m_auditRepo;
    // raw-file layer:
    persistence::AuditTrail            m_auditTrail;         // needs dataDir at construction
    persistence::BackupManager         m_backups;
    // services (order matters — dependencies first):
    NotificationService  m_notify;
    AuditService         m_auditSvc;
    AuthService          m_auth;
    MenuService          m_menuSvc;
    CustomerService      m_customerSvc;
    EmployeeService      m_employeeSvc;
    InventoryService     m_inventorySvc;
    OrderService         m_orderSvc;
    BillingService       m_billingSvc;
    ReservationService   m_reservationSvc;
    ReportService        m_reportSvc;
    SettingsService      m_settingsSvc;
    CommandStack         m_commands;
};

} // namespace aluchop::services
```

Bootstrap ordering (binding): the database must be open and migrated **before** any repository
member constructs. Implementation: a private nested helper declared as the FIRST data member —

```cpp
private:
    struct DbBootstrap {                             // runs first: opens DB, migrates, seeds
        DbBootstrap(const QString& dataDir, const QString& menuSeedJsonPath);
        //  → Database::instance().open(dataDir + "/aluchop.db");
        //  → SchemaMigrator(Database::instance()).migrate(menuSeedJsonPath);
    };
    DbBootstrap m_bootstrap;                         // MUST stay the first member, before all repos
```

The constructor initialiser list starts with `m_bootstrap(dataDir, menuSeedJsonPath)`. After all
members are built, the ctor body calls `m_orderSvc.rebuildQueue()` and, if a remember-token
exists, leaves login to `main.cpp` (AppContext never logs anyone in).

### 3.5 `aluchop::gui`

All widget classes carry `Q_OBJECT`. Widget members are raw pointers owned by Qt parents (§9).

#### `gui/ThemeManager.hpp`

```cpp
#include <QObject>
#include <QColor>
#include <QString>
class QApplication;

namespace aluchop::gui {

/// @oop-concept Structures :: a palette is pure data; two const instances define both themes
struct Palette {
    QColor primary, secondary, accent, background, card, border, success, danger, text;
    QColor textMuted, hover, shadow;                 // derived working colours
};

class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum class Mode { Light, Dark };
    static ThemeManager& instance();

    Mode mode() const noexcept { return m_mode; }
    void setMode(Mode m);                            // regenerates QSS, applies, emits themeChanged
    void toggle();
    const Palette& palette() const noexcept;         // return by reference — current theme's palette
    QString styleSheet() const;                      // full app QSS generated from palette()
    void apply(QApplication& app);                   // app.setStyleSheet(styleSheet())

    static const Palette kLight;                     // exact SPEC §1 hex values — const objects
    static const Palette kDark;                      // derived deep desaturated green-greys

signals:
    void themeChanged();

private:
    explicit ThemeManager(QObject* parent = nullptr);
    Mode m_mode = Mode::Light;
};

} // namespace aluchop::gui
```

`kLight` uses SPEC §1 verbatim. `kDark` (binding values): background `#141A15`, card `#1C241D`,
border `#2A3529`, text `#E4EBE2`, textMuted `#8FA08D`, primary `#7E9B84`, secondary `#5D7A66`,
accent `#A8C3A1`, success `#6FBF77`, danger `#E07A7A`, hover `#232D24`, shadow `#000000` @ 40 %.

#### `gui/Page.hpp` — abstract page base

```cpp
#include <QWidget>
namespace aluchop::services { class AppContext; }

namespace aluchop::gui {

/// @oop-concept Hierarchical Inheritance (GUI) :: nine concrete pages specialise one abstract Page
class Page : public QWidget {
    Q_OBJECT
public:
    explicit Page(services::AppContext& ctx, QWidget* parent = nullptr);
    ~Page() override = default;
    virtual QString pageTitle() const = 0;
    virtual void refresh() = 0;                      // re-query services and repopulate
protected:
    services::AppContext& m_ctx;
};

} // namespace aluchop::gui
```

#### `gui/MainWindow.hpp`

```cpp
#include <QMainWindow>
#include <array>
namespace aluchop::services { class AppContext; }
class QStackedWidget; class QLabel;

namespace aluchop::gui {
class Sidebar; class Page; class ToastHost; class CommandPalette;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(services::AppContext& ctx, QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onNavigate(int pageIndex);
    void onDataChanged(const QString& domain);       // refresh() the current page if affected
    void onNotification(const QString& title, const QString& message, int level);
    void onUndo();
    void onRedo();
    void onToggleTheme();
    void onOpenPalette();

private:
    void buildShortcuts();
        // Ctrl+K palette · Ctrl+Z undo · Ctrl+Shift+Z redo · Ctrl+T theme ·
        // Ctrl+1..9 navigate pages · Ctrl+N new order · F5 refresh current page
    void buildFooter();                              // SPEC §10 credit, subtle, centred

    services::AppContext& m_ctx;
    Sidebar* m_sidebar = nullptr;
    QStackedWidget* m_stack = nullptr;
    std::array<Page*, 9> m_pages{};                  // Dashboard…Settings, index = sidebar order
    QLabel* m_footer = nullptr;
    ToastHost* m_toasts = nullptr;
    CommandPalette* m_palette = nullptr;
};

} // namespace aluchop::gui
```

#### `gui/Sidebar.hpp`

```cpp
#include <QWidget>
class QToolButton;

namespace aluchop::gui {

class Sidebar : public QWidget {
    Q_OBJECT
public:
    explicit Sidebar(QWidget* parent = nullptr);
    void addEntry(const QString& iconSvgPath, const QString& label);   // order = page index
    void setActive(int index);
signals:
    void navigate(int index);
private:
    std::vector<QToolButton*> m_buttons;
    int m_active = 0;
};

} // namespace aluchop::gui
```

#### `gui/SplashScreen.hpp`, `gui/LoginWindow.hpp`

```cpp
// SplashScreen.hpp
#include <QSplashScreen>
namespace aluchop::gui {
class SplashScreen : public QSplashScreen {
    Q_OBJECT
public:
    SplashScreen();                                  // paints logo + app name on sage backdrop
    void showFor(int ms, const std::function<void()>& then);   // fade in, wait, fade out, invoke
};
} // namespace aluchop::gui

// LoginWindow.hpp
#include <QWidget>
namespace aluchop::services { class AppContext; }
class QLineEdit; class QCheckBox; class QPushButton; class QLabel;
namespace aluchop::gui {
class LoginWindow : public QWidget {
    Q_OBJECT
public:
    explicit LoginWindow(services::AppContext& ctx, QWidget* parent = nullptr);
signals:
    void loggedIn();                                 // MainWindow is created by main.cpp on this
private slots:
    void onLoginClicked();                           // auth().login(...) → error label or emit
    void onForgotPassword();                         // inline 3-step: username → question → answer+new pass
private:
    services::AppContext& m_ctx;
    QLineEdit* m_username = nullptr; QLineEdit* m_password = nullptr;
    QCheckBox* m_remember = nullptr; QPushButton* m_loginBtn = nullptr;
    QLabel* m_error = nullptr;
};
} // namespace aluchop::gui
```

#### The nine pages

All: `class XPage final : public Page { Q_OBJECT public: explicit XPage(services::AppContext& ctx, QWidget* parent = nullptr); QString pageTitle() const override; void refresh() override; ... };`
Only page-specific extras are listed:

```cpp
class DashboardPage final : public Page {
    Q_OBJECT
public:
    explicit DashboardPage(services::AppContext& ctx, QWidget* parent = nullptr);
    QString pageTitle() const override;              // "Dashboard"
    void refresh() override;
private:
    void animateCards();                             // staggered QPropertyAnimation fade+rise on load
    std::array<StatCard*, 4> m_cards{};              // Today's Sales · Pending Orders · Customers · Low Stock
    QChartView* m_revenueChart = nullptr;            // QtCharts 7-day bar series from weeklySales()
    QListWidget* m_alerts = nullptr;                 // low stock + expiring + today's reservations
    QTableWidget* m_pendingOrders = nullptr;
    QListWidget* m_popularItems = nullptr;
};

class MenuPage final : public Page {
    Q_OBJECT
public: /* ctor, pageTitle "Menu", refresh */
private slots:
    void onSearch(); void onCategoryChanged(); void onSortChanged();
    void onToggleAvailability();                     // via CommandStack (undoable)
    void onAddItem(); void onEditItem(); void onDeleteItem();
private:
    QLineEdit* m_search; QComboBox* m_category; QComboBox* m_sort;
    QTableWidget* m_table;                           // Name | Category | Price | Available | Description
};

class OrdersPage final : public Page {
    Q_OBJECT
public: /* ctor, pageTitle "Orders", refresh */
private slots:
    void onNewOrder(); void onEditOrder(); void onCancelOrder();
    void onAddItemToOrder();                         // undoable (AddOrderItemCommand)
    void onRemoveItemFromOrder();                    // undoable
    void onSubmitToKitchen(); void onAdvanceStatus();
    void onSplit();                                  // multi-select item lines → splitOrder
    void onMerge();                                  // pick second order → mergeOrders
    void onBill();                                   // opens BillingDialog for Served orders
private:
    QTableWidget* m_orderList;                       // active orders
    QListWidget* m_kitchenBoard;                     // Pending/Preparing/Ready columns rendered as sections
    QTableWidget* m_itemsView;                       // selected order's lines
};

class CustomersPage final : public Page {            // search, CRUD, loyalty, history, favourites
    Q_OBJECT
private slots: void onSearch(); void onAdd(); void onEdit(); void onDelete(); void onShowHistory();
private: QLineEdit* m_search; QTableWidget* m_table; QTableWidget* m_history; QLabel* m_favourites;
};

class EmployeesPage final : public Page {            // staff table, attendance, payroll
    Q_OBJECT
private slots: void onHire(); void onEdit(); void onDeactivate(); void onMarkAttendance(); void onShowPayroll();
private: QTableWidget* m_table; QTableWidget* m_attendance; QTableWidget* m_payroll;
};

class InventoryPage final : public Page {            // ingredients, restock (undoable), suppliers
    Q_OBJECT
private slots: void onAddIngredient(); void onEditIngredient(); void onRestock(); void onWaste();
               void onAddSupplier(); void onEditSupplier();
private: QTableWidget* m_ingredients; QTableWidget* m_suppliers; QListWidget* m_alerts;
};

class ReservationsPage final : public Page {         // day picker, bookings list, availability
    Q_OBJECT
private slots: void onDayChanged(); void onBook(); void onEdit(); void onCancel(); void onSeat(); void onComplete();
private: QDateEdit* m_day; QTableWidget* m_list; QComboBox* m_tablePicker;
};

class ReportsPage final : public Page {              // kind picker, date range, chart, CSV/PDF export
    Q_OBJECT
private slots: void onKindChanged(); void onExportCsv(); void onExportPdf(); void onVerifyAudit();
private: QComboBox* m_kind; QDateEdit* m_from; QDateEdit* m_to;
         QChartView* m_chart; QTableWidget* m_preview;
};

class SettingsPage final : public Page {             // restaurant info, theme, backup/restore
    Q_OBJECT
private slots: void onSaveInfo(); void onThemeToggled(); void onBackup(); void onRestore(); void onExportAll();
private: QLineEdit* m_name; QLineEdit* m_address; QLineEdit* m_phone;
         QComboBox* m_theme; QListWidget* m_backups;
};
```

#### `gui/BillingDialog.hpp`

```cpp
#include <QDialog>
#include "aluchop/models/Bill.hpp"
namespace aluchop::services { class AppContext; }
class QLineEdit; class QComboBox; class QLabel; class QTableWidget;

namespace aluchop::gui {

class BillingDialog : public QDialog {
    Q_OBJECT
public:
    BillingDialog(services::AppContext& ctx, int orderId, QWidget* parent = nullptr);
private slots:
    void onApplyPromo();                             // re-prepareBill with code; shows discount line
    void onMethodChanged();                          // Cash shows tendered field; Card/Wallet lock it
    void onTenderedEdited();                         // live change display via billing().changeFor
    void onSettle();                                 // settle → receipt preview → optional print/PDF
    void onPrintReceipt();
private:
    services::AppContext& m_ctx;
    int m_orderId;
    models::Bill m_bill;                             // current prepared bill (value member)
    QTableWidget* m_lines; QLineEdit* m_promo; QComboBox* m_method;
    QLineEdit* m_tendered; QLabel* m_subtotal; QLabel* m_discount;
    QLabel* m_serviceCharge; QLabel* m_total; QLabel* m_change;
};

} // namespace aluchop::gui
```

#### `gui/StatCard.hpp`, `gui/Toast.hpp`, `gui/CommandPalette.hpp`, `gui/PdfExporter.hpp`

```cpp
// StatCard.hpp
#include <QFrame>
class QLabel;
namespace aluchop::gui {
class StatCard : public QFrame {
    Q_OBJECT
public:
    StatCard(const QString& title, const QString& iconSvgPath, QWidget* parent = nullptr);
    void setValue(const QString& value);
    void setDelta(const QString& delta, bool positive);   // "+12% vs yesterday"
    void animateIn(int delayMs = 0);                 // opacity 0→1 + 12px rise, QPropertyAnimation
private:
    QLabel* m_title; QLabel* m_value; QLabel* m_delta;
};
} // namespace aluchop::gui

// Toast.hpp
#include <QWidget>
namespace aluchop::gui {
class ToastHost : public QWidget {                   // overlay pinned to MainWindow's corner
    Q_OBJECT
public:
    explicit ToastHost(QWidget* parent);
    void show(const QString& title, const QString& message, int level, int ms = 3500);
        // stacks up to 4; each fades in, auto-dismisses with QPropertyAnimation fade-out
};
} // namespace aluchop::gui

// CommandPalette.hpp
#include <QDialog>
namespace aluchop::services { class AppContext; }
class QLineEdit; class QListWidget;
namespace aluchop::gui {
class CommandPalette : public QDialog {              // frameless, glassmorphic, Ctrl+K
    Q_OBJECT
public:
    explicit CommandPalette(services::AppContext& ctx, QWidget* parent = nullptr);
signals:
    void navigateRequested(int pageIndex);
    void openOrderRequested(int orderId);
private slots:
    void onQueryChanged(const QString& q);
        // merges: page names, menu().search(q), customers().search(q), order-number prefix match
    void onActivated();
private:
    services::AppContext& m_ctx;
    QLineEdit* m_input; QListWidget* m_results;
};
} // namespace aluchop::gui

// PdfExporter.hpp  — keeps QtPrintSupport out of services
#include <vector>
#include <QString>
#include <QStringList>
#include "aluchop/core/Result.hpp"
#include "aluchop/models/Bill.hpp"
class QWidget;
namespace aluchop::gui {
class PdfExporter {
public:
    static core::Result<QString> exportReportPdf(const QString& title, const QStringList& header,
                                                 const std::vector<QStringList>& rows,
                                                 const QString& outPath);
        // QTextDocument HTML table → QPdfWriter; footer carries kAppInfo credit
    static core::Result<QString> receiptPdf(const models::Bill& bill, const QString& outPath);
    static void printReceipt(const models::Bill& bill, QWidget* parent);   // QPrintDialog + QPrinter
};
} // namespace aluchop::gui
```

#### `src/main.cpp` (binding flow)

```cpp
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    aluchop::gui::ThemeManager::instance().apply(app);       // theme first (reads default Light)

    std::unique_ptr<aluchop::services::AppContext> ctx;
    try {
        ctx = std::make_unique<aluchop::services::AppContext>(dataDir(), seedPath());
        // dataDir() = QStandardPaths::AppDataLocation + "/AluChop"; seedPath() = "assets/menu/menu_seed.json"
    }
    /// @oop-concept Multiple Catch :: each failure mode gets its own handler and message
    catch (const aluchop::core::DatabaseException& e) { /* QMessageBox critical, return 1 */ }
    catch (const aluchop::core::FileIOException& e)  { /* QMessageBox critical, return 1 */ }
    catch (const aluchop::core::AluChopException& e) { /* QMessageBox critical, return 1 */ }
    catch (const std::exception& e)                  { /* QMessageBox critical, return 1 */ }

    // apply persisted theme: ctx->settings().get("theme.mode") == "dark" → setMode(Dark)
    // splash (1200 ms fade) → tryRememberedLogin() ? MainWindow : LoginWindow → MainWindow
    return app.exec();
}
```

`MainWindow` is created on the heap with no parent and `Qt::WA_DeleteOnClose`; `ctx` outlives it
(destroyed after `app.exec()` returns).

---

## 4. The inheritance graph

```
                        ┌──────────────────┐
                        │  Person (abstract)│  models/Person.hpp
                        └───────┬───────────┘
             virtual public ────┤──── virtual public          ← VIRTUAL BASE CLASS (diamond root)
            ┌───────────────────┴────────────────────┐
   ┌────────┴─────────┐                    ┌──────────┴────────┐
   │     Employee     │                    │     Customer      │
   └┬────┬────┬────┬──┘                    └─────────┬─────────┘
    │pub │pub │pub │public                           │
    │    │    │    └──────────────┐                  │
┌───┴──┐┌┴───┐┌────┴───┐   ┌──────┴──────────────────┴──┐
│Waiter││Chef││Manager │   │       StaffCustomer        │  ← THE DIAMOND (hybrid):
└──────┘└────┘└───┬────┘   │ : public Employee,         │    Employee + Customer, ONE Person
                  │public  │   public Customer          │    subobject thanks to virtual bases
             ┌────┴─────┐  └────────────────────────────┘
             │  Admin   │ : public Manager, public IAuditable   ← MULTIPLE inheritance
             └──────────┘   (Person→Employee→Manager→Admin = MULTILEVEL)

Interfaces (pure abstract, public inheritance):
  IPrintable   ← Order, Bill
  ISerializable← MenuItem
  IAuditable   ← Admin
  IDiscountable← Bill              (Bill : public IPrintable, public IDiscountable = MULTIPLE)

GUI hierarchy (public, hierarchical):
  QWidget ← Page (abstract) ← DashboardPage, MenuPage, OrdersPage, CustomersPage,
            EmployeesPage, InventoryPage, ReservationsPage, ReportsPage, SettingsPage (all final)

Template-base inheritance (public):
  Repository<T> ← UserRepository, MenuRepository, CustomerRepository, EmployeeRepository,
                  OrderRepository, IngredientRepository, SupplierRepository, TableRepository,
                  ReservationRepository, PaymentRepository, PromoRepository

Command hierarchy (public, hierarchical, runtime polymorphism):
  Command (abstract) ← AddOrderItemCommand, RemoveOrderItemCommand,
                       ToggleAvailabilityCommand, AdjustStockCommand (all final)

Report hierarchy:
  CsvWriter ◄─protected─ ReportGenerator (abstract) ◄─public─ SalesReport, InventoryReport,
                                                              OrdersReport, CustomersReport,
                                                              EmployeesReport (all final)

Private inheritance:
  BinaryRecordFile ◄─private─ AuditTrail
```

Where each required form lives — the checklist:

| Form | Exact site | Why it is genuine |
|---|---|---|
| **Single** | `Manager : public Employee` | one base, adds bonus/pay policy |
| **Multiple** | `Admin : public Manager, public IAuditable`; also `Bill : public IPrintable, public IDiscountable` | role + capability; document + discount target |
| **Hierarchical** | `Waiter/Chef/Manager : public Employee`; `9 pages : public Page`; commands; reports | one base, many siblings, everywhere |
| **Multilevel** | `Person → Employee → Manager → Admin` | each level adds real state/behaviour |
| **Hybrid** | `StaffCustomer` (multiple over a hierarchical/virtual structure) + `Admin` (multilevel + multiple) | the people graph combines every form |
| **Public** | all of the above | is-a |
| **Protected** | `ReportGenerator : protected persistence::CsvWriter` | implemented-in-terms-of, and *derived* reports must also drive the writer — protected is exactly right |
| **Private** | `AuditTrail : private BinaryRecordFile` | implemented-in-terms-of only; raw `append`/`overwriteAt` must stay unreachable so seq/checksum invariants hold; `using BinaryRecordFile::close` selectively re-exposes |
| **Virtual base class** | `Employee : virtual public Person`, `Customer : virtual public Person` | without it, `StaffCustomer` (a waiter enrolled in the loyalty programme) would have two ids, two names, ambiguous `name()` — a real diamond genuinely solved |
| **Method overriding** | `monthlyPay()` in Waiter/Chef/Manager; `roleName()` everywhere (`final` in Admin); `refresh()` in pages; `fromRecord()` in repos | payroll, labels, UI refresh are honestly polymorphic |

---

## 5. OOP concept map

Concept → host → why natural. This is the source for `docs/OOP_COVERAGE.md` (which adds file:line after build).

| # | Concept | Host (class / file) | Why it is natural there |
|---|---|---|---|
| 5.1 | Functions | free helpers `models/Enums.cpp` (`toString`/`fromString`) | enum↔DB-token mapping |
| 5.1 | Function overloading | `Logger::log(QString)` / `log(Level, QString)`; `Order::addItem(...)` ×2; `AuditService::log` ×2 | same verb, different detail levels |
| 5.1 | Inline functions | `core::formatNpr` (explicit `inline`); Money accessors (in-class) | hot-path formatting/observers |
| 5.1 | Default arguments | `Money::fromRupees(r, paisa = 0)`; `MenuService::search(...)`; `OrderService::createOrder(...)`; `InventoryService::expiring(days = 7)` | sensible call-site defaults |
| 5.1 | Pass by reference | every service ctor (repos by `&`); `const QString&` params throughout | no copies, explicit wiring |
| 5.1 | Return by reference | `Order::operator[]`, `Money::operator+=`, `Database::handle()`, `Person::name()`, `ThemeManager::palette()` | callers mutate/observe in place |
| 5.1 | Arrays | `AuditRecord::action[16]/entity[16]/details[64]` (char arrays); `std::array` uses below | fixed binary layout demands raw arrays |
| 5.1 | Strings | `QString` domain-wide; `char[]`+`std::string` in file/exception layer | both string families used where apt |
| 5.1 | Pointers | widget members (`QLabel*` …); `std::unique_ptr<Employee>` staff lists | Qt ownership + polymorphic handles |
| 5.1 | Dynamic memory allocation | `new` for every widget (Qt parent-owned); `std::make_unique` for reports/commands/context | GUI + factories genuinely heap-allocate |
| 5.1 | Structures | `AuditRecord`, `RecipeLine`, `Palette`, `AppInfo` | pure data, no invariants |
| 5.1 | Enumerations | `models/Enums.hpp` (7 scoped enums); `Logger::Level`; `ThemeManager::Mode` | closed vocabularies |
| 5.1 | Namespaces | `aluchop::core/models/persistence/services/gui` | the layer map itself |
| 5.1 | Constants | `Chef::kOvertimeRatePerHour`, `AuthService::kMinPasswordLength`, `CommandStack::kMaxDepth`, `SchemaMigrator::kLatestVersion` | domain constants, not magic numbers |
| 5.2 | Multiple classes / objects | entire codebase (~60 classes) | — |
| 5.2 | Constructors / parameterised | every model has default + parameterised ctors | hydration vs construction |
| 5.2 | Destructor | `Order::~Order` (open counter), `BinaryRecordFile::~BinaryRecordFile`, `CsvWriter::~CsvWriter`, `Logger::~Logger` (RAII close) | resources genuinely need releasing |
| 5.2 | Copy constructor | `Order(const Order&)` — deep copy, id reset | split-bill copies an order for real |
| 5.2 | Objects as members | `AppContext` (repos+services by value); `Order::m_items`; `BillingDialog::m_bill` | composition root |
| 5.2 | Object arrays | `std::array<core::Money,7> ReportService::weeklySales()`; `core::kMenuCategories`; `std::array<StatCard*,4>` | week = 7 Money objects; 14 categories |
| 5.2 | Object pointers | `std::vector<std::unique_ptr<models::Employee>>` staff; `Page* m_pages[9]` | polymorphic collections |
| 5.2 | Dynamic objects | `EmployeeRepository::makeTyped` (heap role objects); `ReportService::makeReport` | factories must allocate the runtime type |
| 5.2 | Static members | `Order::s_openCount` + `openOrderCount()`; `Logger::s_messageCount`; `Database::instance()`; `ThemeManager::instance()` | process-wide state |
| 5.2 | Constant objects | `core::kAppInfo`, `kMenuCategories`, `ThemeManager::kLight/kDark`, `Chef::kOvertimeRatePerHour` | immutable identity/config |
| 5.2 | Constant member functions | every observer in every model (all `const`, mostly `noexcept`) | const-correctness by default |
| 5.2 | Friend functions | `Money::operator<<`, `Bill::operator<<` | streaming needs internals |
| 5.2 | Friend classes | `Bill` friends `services::BillingService` | only the billing engine may settle a bill |
| 5.3 | `+` `-` | `Money` free operators; unary `-` | arithmetic value type |
| 5.3 | `==` `<` | `Money`, `MenuItem` | equality by id, ordering for sort |
| 5.3 | `<<` | `Money` and `Bill` (`std::ostream`) | text receipts / CSV |
| 5.3 | `[]` | `Order::operator[]` (const + non-const) | an order is a sequence of lines |
| 5.3 | `=` | `Order::operator=` (deep copy) | split/merge editing |
| 5.3 | `++` | `Customer::operator++` prefix + postfix | "one more visit" |
| 5.3 | practical extras | `Money::operator+=/-=/*=`, `Order::operator+=` (merge bills) | domain-meaningful compounds |
| 5.4 | all inheritance forms | see §4 checklist | — |
| 5.5 | Virtual functions | `Employee::monthlyPay`, `Person::displayLabel` | payroll polymorphism |
| 5.5 | Pure virtual / abstract | `Person::roleName`, `Command`, `Page`, `ReportGenerator`, `Repository<T>::fromRecord`, interfaces | genuine incomplete bases |
| 5.5 | Runtime polymorphism | payroll loop, `CommandStack`, page `refresh()`, report export | dispatch on real heterogeneity |
| 5.5 | Compile-time polymorphism | overloads above + `Repository<T>`, `Result<T>`, `sumMoney` | templates + overloading |
| 5.6 | Read / Write / Append | `BinaryRecordFile` (r/w), `Logger` (append), `CsvWriter` (write) | three distinct real files |
| 5.6 | Binary + Random access | `BinaryRecordFile::readAt/overwriteAt` via `seekg`/`seekp` on 128-byte records | audit trail lookup by index |
| 5.6 | ASCII + Sequential | `CsvWriter` row-by-row; `Logger` line-append | exports and logs |
| 5.6 | Error checking | every stream op in all three classes checks state and throws `FileIOException`; `BackupManager::isValidSqliteFile` header check | binding contract in §3.3 |
| 5.7 | Function templates | `core::sumMoney`, `countMatching`, `clampValue` | money summation everywhere |
| 5.7 | Class templates | `Repository<T>`, `Result<T>` | CRUD skeleton, error carrier |
| 5.7 | STL vector/map/queue | `vector` everywhere; `std::map` in `SettingsService::m_cache`; `std::queue` in `KitchenQueue` | natural containers |
| 5.7 | Algorithms + iterators | `std::sort` in `MenuService::search`; explicit iterator loop in `sumMoney`; `std::find_if` in `KitchenQueue::remove` | real use, not demos |
| 5.8 | try/catch/throw | services convert persistence throws to `Result::err`; models throw `ValidationException` in setters | validation + IO boundaries |
| 5.8 | Multiple catch | `main.cpp` context construction; `OrderService::advanceStatus` (Inventory vs Database) | different recovery per type |
| 5.8 | **Rethrow** | `Database::transaction` — rollback then `throw;`; `AuditService::log` — log then `throw;` | rollback/log must not swallow |
| 5.8 | Custom hierarchy | `core/Exceptions.hpp` — `AluChopException` + 5 children | exactly SPEC §5.8 |

---

## 6. SQLite schema

Executed by `SchemaMigrator::applyMigration1()` inside one transaction. Every connection runs
`PRAGMA foreign_keys = ON;` (done in `Database::open`). All money columns are **INTEGER paisa**.
All timestamps TEXT ISO-8601 UTC (`yyyy-MM-ddTHH:mm:ss`), dates TEXT `yyyy-MM-dd`, times TEXT `HH:mm`.

```sql
CREATE TABLE settings (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE suppliers (
  id      INTEGER PRIMARY KEY AUTOINCREMENT,
  name    TEXT NOT NULL,
  phone   TEXT,
  email   TEXT,
  address TEXT
);

CREATE TABLE ingredients (
  id                  INTEGER PRIMARY KEY AUTOINCREMENT,
  name                TEXT NOT NULL UNIQUE,
  unit                TEXT NOT NULL,
  stock_qty           REAL NOT NULL DEFAULT 0 CHECK (stock_qty >= 0),
  low_stock_threshold REAL NOT NULL DEFAULT 0 CHECK (low_stock_threshold >= 0),
  expiry_date         TEXT,
  unit_cost_paisa     INTEGER NOT NULL DEFAULT 0 CHECK (unit_cost_paisa >= 0),
  supplier_id         INTEGER REFERENCES suppliers(id) ON DELETE SET NULL
);

CREATE TABLE menu_items (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  name         TEXT NOT NULL,
  category     TEXT NOT NULL,
  price_paisa  INTEGER NOT NULL CHECK (price_paisa >= 0),
  description  TEXT NOT NULL DEFAULT '',
  image_path   TEXT NOT NULL DEFAULT '',
  is_available INTEGER NOT NULL DEFAULT 1 CHECK (is_available IN (0,1)),
  created_at   TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))
);

CREATE TABLE recipes (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  menu_item_id    INTEGER NOT NULL REFERENCES menu_items(id) ON DELETE CASCADE,
  ingredient_id   INTEGER NOT NULL REFERENCES ingredients(id) ON DELETE CASCADE,
  qty_per_serving REAL NOT NULL CHECK (qty_per_serving > 0),
  UNIQUE (menu_item_id, ingredient_id)
);

CREATE TABLE employees (
  id                 INTEGER PRIMARY KEY AUTOINCREMENT,
  name               TEXT NOT NULL,
  phone              TEXT,
  email              TEXT,
  position           TEXT NOT NULL CHECK (position IN ('WAITER','CHEF','MANAGER','ADMIN')),
  salary_paisa       INTEGER NOT NULL DEFAULT 0 CHECK (salary_paisa >= 0),
  shift              TEXT NOT NULL DEFAULT 'DAY',
  hired_date         TEXT,
  is_active          INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0,1)),
  performance_rating INTEGER NOT NULL DEFAULT 3 CHECK (performance_rating BETWEEN 1 AND 5)
);

CREATE TABLE attendance (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  employee_id INTEGER NOT NULL REFERENCES employees(id) ON DELETE CASCADE,
  work_date   TEXT NOT NULL,
  check_in    TEXT,
  check_out   TEXT,
  status      TEXT NOT NULL DEFAULT 'PRESENT' CHECK (status IN ('PRESENT','ABSENT','LEAVE')),
  UNIQUE (employee_id, work_date)
);

CREATE TABLE customers (
  id             INTEGER PRIMARY KEY AUTOINCREMENT,
  name           TEXT NOT NULL,
  phone          TEXT UNIQUE,
  email          TEXT,
  loyalty_points INTEGER NOT NULL DEFAULT 0 CHECK (loyalty_points >= 0),
  visits         INTEGER NOT NULL DEFAULT 0 CHECK (visits >= 0),
  created_at     TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))
);

CREATE TABLE users (
  id                   INTEGER PRIMARY KEY AUTOINCREMENT,
  username             TEXT NOT NULL UNIQUE,
  pass_hash            TEXT NOT NULL,
  salt                 TEXT NOT NULL,
  role                 TEXT NOT NULL CHECK (role IN ('ADMIN','MANAGER','WAITER','CHEF')),
  employee_id          INTEGER REFERENCES employees(id) ON DELETE SET NULL,
  security_question    TEXT NOT NULL DEFAULT '',
  security_answer_hash TEXT NOT NULL DEFAULT '',
  remember_token       TEXT NOT NULL DEFAULT '',
  created_at           TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))
);

CREATE TABLE tables (
  id        INTEGER PRIMARY KEY AUTOINCREMENT,
  name      TEXT NOT NULL UNIQUE,
  capacity  INTEGER NOT NULL CHECK (capacity >= 1),
  is_active INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0,1))
);

CREATE TABLE orders (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  order_number TEXT NOT NULL UNIQUE,
  type         TEXT NOT NULL CHECK (type IN ('DINE_IN','TAKEAWAY','DELIVERY')),
  status       TEXT NOT NULL DEFAULT 'OPEN'
               CHECK (status IN ('OPEN','PENDING','PREPARING','READY','SERVED','PAID','CANCELLED')),
  table_id     INTEGER REFERENCES tables(id) ON DELETE SET NULL,
  customer_id  INTEGER REFERENCES customers(id) ON DELETE SET NULL,
  waiter_id    INTEGER REFERENCES employees(id) ON DELETE SET NULL,
  note         TEXT NOT NULL DEFAULT '',
  merged_into  INTEGER REFERENCES orders(id) ON DELETE SET NULL,
  created_at   TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))
);

CREATE TABLE order_items (
  id               INTEGER PRIMARY KEY AUTOINCREMENT,
  order_id         INTEGER NOT NULL REFERENCES orders(id) ON DELETE CASCADE,
  menu_item_id     INTEGER REFERENCES menu_items(id) ON DELETE SET NULL,
  name_snapshot    TEXT NOT NULL,
  unit_price_paisa INTEGER NOT NULL CHECK (unit_price_paisa >= 0),
  qty              INTEGER NOT NULL CHECK (qty >= 1),
  line_note        TEXT NOT NULL DEFAULT ''
);

CREATE TABLE reservations (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  customer_id     INTEGER REFERENCES customers(id) ON DELETE SET NULL,
  customer_name   TEXT NOT NULL,
  phone           TEXT,
  table_id        INTEGER NOT NULL REFERENCES tables(id) ON DELETE CASCADE,
  starts_at       TEXT NOT NULL,
  duration_min    INTEGER NOT NULL DEFAULT 90 CHECK (duration_min >= 15),
  guests          INTEGER NOT NULL CHECK (guests >= 1),
  special_request TEXT NOT NULL DEFAULT '',
  status          TEXT NOT NULL DEFAULT 'BOOKED'
                  CHECK (status IN ('BOOKED','SEATED','COMPLETED','CANCELLED','NO_SHOW'))
);

CREATE TABLE promos (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  code            TEXT NOT NULL UNIQUE,
  kind            TEXT NOT NULL CHECK (kind IN ('PERCENT','FLAT')),
  percent         INTEGER NOT NULL DEFAULT 0 CHECK (percent BETWEEN 0 AND 100),
  flat_paisa      INTEGER NOT NULL DEFAULT 0 CHECK (flat_paisa >= 0),
  min_order_paisa INTEGER NOT NULL DEFAULT 0 CHECK (min_order_paisa >= 0),
  valid_from      TEXT,
  valid_to        TEXT,
  is_active       INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0,1))
);

CREATE TABLE payments (
  id                   INTEGER PRIMARY KEY AUTOINCREMENT,
  order_id             INTEGER NOT NULL REFERENCES orders(id) ON DELETE CASCADE,
  method               TEXT NOT NULL CHECK (method IN ('CASH','CARD','WALLET')),
  subtotal_paisa       INTEGER NOT NULL CHECK (subtotal_paisa >= 0),
  discount_paisa       INTEGER NOT NULL DEFAULT 0 CHECK (discount_paisa >= 0),
  service_charge_paisa INTEGER NOT NULL DEFAULT 0 CHECK (service_charge_paisa >= 0),
  total_paisa          INTEGER NOT NULL CHECK (total_paisa >= 0),
  tendered_paisa       INTEGER NOT NULL CHECK (tendered_paisa >= 0),
  change_paisa         INTEGER NOT NULL DEFAULT 0 CHECK (change_paisa >= 0),
  promo_id             INTEGER REFERENCES promos(id) ON DELETE SET NULL,
  received_by          INTEGER REFERENCES users(id) ON DELETE SET NULL,
  paid_at              TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))
);

CREATE TABLE inventory_transactions (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  ingredient_id   INTEGER NOT NULL REFERENCES ingredients(id) ON DELETE CASCADE,
  delta_qty       REAL NOT NULL,
  reason          TEXT NOT NULL CHECK (reason IN ('RESTOCK','USAGE','WASTE','ADJUST')),
  ref_order_id    INTEGER REFERENCES orders(id) ON DELETE SET NULL,
  unit_cost_paisa INTEGER NOT NULL DEFAULT 0,
  note            TEXT NOT NULL DEFAULT '',
  created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))
);

CREATE TABLE audit_log (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  seq          INTEGER NOT NULL,
  ts_utc_ms    INTEGER NOT NULL,
  user_id      INTEGER NOT NULL DEFAULT 0,
  action       TEXT NOT NULL,
  entity       TEXT NOT NULL,
  amount_paisa INTEGER NOT NULL DEFAULT 0,
  details      TEXT NOT NULL DEFAULT ''
);

CREATE INDEX idx_orders_created      ON orders(created_at);
CREATE INDEX idx_orders_status       ON orders(status);
CREATE INDEX idx_order_items_order   ON order_items(order_id);
CREATE INDEX idx_reservations_start  ON reservations(starts_at);
CREATE INDEX idx_reservations_table  ON reservations(table_id, starts_at);
CREATE INDEX idx_invtx_ingredient    ON inventory_transactions(ingredient_id, created_at);
CREATE INDEX idx_payments_paid_at    ON payments(paid_at);
CREATE INDEX idx_audit_ts            ON audit_log(ts_utc_ms);
CREATE INDEX idx_menu_category       ON menu_items(category, name);
CREATE INDEX idx_customers_phone     ON customers(phone);
CREATE INDEX idx_attendance_emp      ON attendance(employee_id, work_date);
```

### Seed / migration strategy (binding)

- Versioning: `settings['schema_version']`, absent → 0. `SchemaMigrator::migrate` applies each
  migration `n → n+1` inside `Database::transaction`, then writes the new version. Only
  migration 1 exists at launch; future changes append `applyMigration2()` etc. — never edit old DDL.
- Seeding runs once, immediately after migration 1, still inside the transaction:
  1. **Menu**: parse `assets/menu/menu_seed.json` — top-level array of objects with keys
     `name, category, price_paisa, description, image, available` (matches `MenuItem::fromJson`).
     All 14 categories populated, ≥ 4 items each. Malformed file → `ValidationException` (startup aborts).
  2. **Admin**: employee "Shashank Bhattarai" (position ADMIN) + user `admin` /
     password `admin123`, security question `"What is your roll number?"` → answer hash of `"ACE082BCT078"`.
  3. **Tables**: `T1…T12`, capacities `2,2,2,4,4,4,4,6,6,6,8,8`.
  4. **Suppliers**: 3 sample Kathmandu suppliers.
  5. **Ingredients**: ~20 staples (rice, flour, chicken, paneer, oil, salmon, nori, cheese, tomato,
     onion, garlic, butter, sugar, milk, cream, tea, coffee beans, hops-free: bottled beer stock, wine bottles, lime)
     with sensible units/thresholds; every seeded menu item gets a 1–3-line recipe.
  6. **Promos**: `WELCOME10` (10 % percent promo, active, no window), `FLAT100` (Rs 100 flat, min order Rs 1000).
- `menu_seed.json` is read with `QFile`/`QJsonDocument` (persistence layer owns this — GUI never parses it).

---

## 7. Raw file-handling design (`<fstream>` — the syllabus layer)

Three genuinely distinct file mechanisms, all specified fully in §3 — summary of ownership:

| Mechanism | Class (owner) | File | Mode | Access pattern |
|---|---|---|---|---|
| Fixed-record **binary** log | `persistence::BinaryRecordFile` → wrapped by `persistence::AuditTrail` (private inheritance) | `<dataDir>/audit.bin` | `std::fstream`, `in\|out\|binary` | **random access**: `seekg/seekp(index * 128)`; append via `seekp(0, end)` |
| Sequential **ASCII** export | `persistence::CsvWriter` (driven by `services::ReportGenerator` via protected inheritance) | `reports/*.csv` | `std::ofstream`, `trunc` | sequential row writes |
| **Append**-mode log | `core::Logger` | `logs/aluchop.log` | `std::ofstream`, `app` | append-only lines |
| Binary **read** validation | `persistence::BackupManager::isValidSqliteFile` | any `.db` | `std::ifstream`, `binary` | sequential 16-byte header read |

The 128-byte `AuditRecord` layout (byte-exact, zero padding — asserted at compile time):

| Offset | Size | Field | Encoding |
|---|---|---|---|
| 0 | 8 | `timestampUtcMs` | `int64` little-endian (native; file is machine-local) |
| 8 | 8 | `amountPaisa` | `int64` |
| 16 | 4 | `magic` | `0x414C4348` (`'A' 'L' 'C' 'H'`) |
| 20 | 4 | `seq` | `uint32`, 1-based, strictly increasing |
| 24 | 4 | `userId` | `uint32`, 0 = system |
| 28 | 16 | `action` | ASCII, NUL-padded (e.g. `ORDER_PAID`, `LOGIN`, `RESTOCK`) |
| 44 | 16 | `entity` | ASCII, NUL-padded (e.g. `order:42`) |
| 60 | 64 | `details` | ASCII, NUL-padded, truncated |
| 124 | 4 | `checksum` | additive `uint32` over bytes `[0,124)` |

Who writes it: **only** `services::AuditService::log(...)` → `AuditTrail::record(...)`. Every
mutating service action calls `AuditService::log` (order created/paid, login/logout, restock,
backup, settings change, user created). ReportsPage's "Verify audit integrity" button runs
`AuditTrail::verifyIntegrity` and reads arbitrary records by index — the visible proof of random access.

Error-checking contract (restated, binding): every `open/seekg/seekp/read/write/flush/close` is
followed by a stream-state check; failure throws `core::FileIOException` with the path and
operation in the message. Destructors close quietly (never throw).

---

## 8. Templates design

| Template | Kind | Lives in | Signature (exact) |
|---|---|---|---|
| `Result<T>` + `Result<void>` | class template + full specialisation | `core/Result.hpp` | §3.1 — service-boundary error carrier; GUI branches on `isOk()` |
| `Repository<T>` | class template with pure-virtual hook | `persistence/Repository.hpp` | §3.3 — `findAll/findById/count/removeById` generic; 11 concrete instantiations |
| `sumMoney` | function template | `core/Algorithms.hpp` | `template <typename Container, typename Projection> Money sumMoney(const Container&, Projection)` — used by `Order::subtotal`, `ReportService`, payroll totals |
| `countMatching` | function template | `core/Algorithms.hpp` | used by dashboard (low-stock count, active-order count) |
| `clampValue` | function template | `core/Algorithms.hpp` | used by `Employee::setPerformanceRating`, animation timing |

Rules: templates are header-only; no explicit instantiation files; `Repository<T>` methods defined
in-class (as shown) so every TU sees them.

---

## 9. Threading and ownership rules

**Threading (binding): the application is single-threaded.** All DB access, file I/O and business
logic run on the GUI thread — TOOLCHAIN gotcha #2 (per-thread `QSqlDatabase`) never triggers. No
`QtConcurrent`, no `QThread`, no worker pools. Long operations (backtest-scale work does not exist
here; the heaviest job is a report export) show a `QProgressDialog` if > 300 ms. Timers
(`QTimer`) are allowed (toast dismissal, splash, animations) — they fire on the GUI thread.
"Auto-save" = every mutation is persisted immediately through its service; there is no dirty state.

**Ownership (binding rules — memorise):**
1. Anything deriving `QObject`/`QWidget`: allocate with `new`, give it a parent at construction
   (or via layout/`addWidget`). **Never** wrap a parented `QObject` in a smart pointer. Never `delete` it manually.
2. Windows without parents (`MainWindow`, `LoginWindow`): heap + `setAttribute(Qt::WA_DeleteOnClose)`.
3. Non-QObject types: value semantics first (`AppContext` members, models); `std::unique_ptr` only
   for runtime-polymorphic ownership (employees-typed, reports, commands). `std::shared_ptr` is **banned**.
4. Services/repos are owned by `AppContext` (values) and passed **by reference**. Nothing stores a
   pointer to a service except widgets storing `services::AppContext& m_ctx` (reference member,
   guaranteed to outlive all windows — `ctx` is destroyed after `app.exec()` returns).
5. Signals across ownership boundaries: connect with context object
   (`connect(&ctx.notifications(), &..., this, ...)`) so connections die with the widget.
6. `Database`/`ThemeManager`/`Logger` are Meyers singletons — function-local statics, destroyed at
   exit in reverse order; none holds references to the others at destruction time.

---

## 10. Screen inventory

| # | Screen / widget | Class | Talks to (services) | Displays / does |
|---|---|---|---|---|
| — | Splash | `SplashScreen` | none | logo + name, 1.2 s fade in/out, then login |
| — | Login | `LoginWindow` | `AuthService` | username/password, remember me, forgot-password (security question), error label |
| — | Shell | `MainWindow` + `Sidebar` | `NotificationService`, `CommandStack`, `SettingsService` | sidebar nav (9 pages), footer credit (SPEC §10), shortcuts, toasts, undo/redo, theme toggle |
| 1 | Dashboard | `DashboardPage` | `ReportService`, `InventoryService`, `ReservationService`, `OrderService` | 4 animated stat cards (today's sales, pending orders, customer count, low-stock count), 7-day revenue bar chart (QtCharts), popular items, alerts list, pending-orders table |
| 2 | Menu | `MenuPage` | `MenuService`, `CommandStack` | search/filter/sort across 14 categories, availability toggle (undoable), item CRUD dialogs, price display via `formatNpr` |
| 3 | Orders | `OrdersPage` + `BillingDialog` | `OrderService`, `BillingService`, `CommandStack` | active orders, kitchen board (Pending→Preparing→Ready→Served), order editor (add/remove lines, undoable), split, merge, bill+settle+receipt |
| 4 | Customers | `CustomersPage` | `CustomerService` | searchable customer table, loyalty points, visits, visit history, favourite items |
| 5 | Employees | `EmployeesPage` | `EmployeeService` | staff table (polymorphic role labels), hire/edit/deactivate, attendance marking + month view, payroll preview (virtual `monthlyPay`) |
| 6 | Inventory | `InventoryPage` | `InventoryService`, `CommandStack` | ingredients with stock/threshold/expiry, low-stock + expiring alerts, restock (undoable) / waste, suppliers CRUD |
| 7 | Reservations | `ReservationsPage` | `ReservationService` | day picker, bookings list with status actions, availability-aware table picker, guests + special requests |
| 8 | Reports | `ReportsPage` | `ReportService`, `AuditService`, gui `PdfExporter` | kind picker (Sales/Inventory/Orders/Customers/Employees), date range, preview table + chart, CSV export (`ReportGenerator::exportCsv` → `reports/`), PDF export (`PdfExporter` → `reports/`), audit-integrity verify |
| 9 | Settings | `SettingsPage` | `SettingsService`, gui `ThemeManager` | restaurant info, Light/Dark theme, backup list + create/restore, export |
| — | Global search | `CommandPalette` | `MenuService`, `CustomerService`, `OrderService` | Ctrl+K fuzzy search: pages, menu items, customers, order numbers → navigate |
| — | Toasts | `ToastHost` | `NotificationService` (signal) | stacked auto-dismissing notifications (low stock, order ready, saved, errors) |

---

## 11. Build integration notes (for the orchestrator's CMake lane)

- `find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Charts Svg PrintSupport)` with the TOOLCHAIN
  prefix-path variables — copy the verified commands verbatim.
- `set(CMAKE_AUTOMOC ON)`; list **headers containing `Q_OBJECT`** in the target sources
  (all `include/aluchop/gui/*.hpp` + `include/aluchop/services/NotificationService.hpp`).
- Target links: `Qt6::Widgets Qt6::Sql Qt6::Charts Qt6::Svg Qt6::PrintSupport`.
- `target_include_directories(aluchop PRIVATE include)`.
- C++17: `set(CMAKE_CXX_STANDARD 17)`, `CMAKE_CXX_STANDARD_REQUIRED ON`.
- No third-party libraries of any kind.

### Standalone single-TU syntax check — corrected recipe

> **`TOOLCHAIN.md`'s single-file recipe does not work on this machine and will waste your time.**
> Homebrew's Qt 6.11.1 is a **framework** build: `/opt/homebrew/opt/qtbase/include` is essentially
> empty (only `QtDeviceDiscoverySupport` / `QtFbSupport`), and the real headers live inside
> `lib/<Module>.framework/Headers`. The `-I …/include/QtCore`-style flags in TOOLCHAIN.md therefore
> resolve to nothing and the very first `#include <QString>` fails with *'QString' file not found*.
> Use the framework flags below — they are byte-for-byte what CMake's `Qt6::*` targets pass
> (verified by reading `compile_commands.json` from a probe project).

```bash
QB=/opt/homebrew/opt/qtbase/lib; QC=/opt/homebrew/opt/qtcharts/lib; QS=/opt/homebrew/opt/qtsvg/lib
clang++ -fsyntax-only -std=c++17 -I include \
  -F $QB -F $QC -F $QS \
  -I $QB/QtCore.framework/Headers   -I $QB/QtGui.framework/Headers \
  -I $QB/QtWidgets.framework/Headers -I $QB/QtSql.framework/Headers \
  -I $QB/QtPrintSupport.framework/Headers \
  -I $QC/QtCharts.framework/Headers \
  -I $QS/QtSvg.framework/Headers -I $QS/QtSvgWidgets.framework/Headers \
  -Wall -Wextra src/path/to/File.cpp
```

---

## 12. Contract-seal reconciliations

The header tree was written by five parallel lanes and then integrated. Every header now compiles
**standalone** (85/85, `-Wall -Wextra`, zero warnings) and all 85 compile **together** in one
translation unit, as an object file, through Qt's own CMake flags. The changes integration forced:

**R1 — `AuditService` grew a GUI-lawful audit-trail surface.** *(LANE-GUI vs LANE-SERVICES)*
`gui/ReportsPage.hpp` documents "Verify audit" as running **`services::AuditService`'s** integrity
check, but `AuditService` exposed only `persistence::AuditTrail& trail()`, and this document's own
§3.4 said "ReportsPage integrity check uses this". Following that would have made a `gui` translation
unit include `persistence/AuditTrail.hpp` — a direct layer violation, and the only one in the tree.
Resolved the way the GUI lane described it: `AuditService` now owns
`verifyTrailIntegrity(std::size_t&)`, `trailRecordCount()`, `trailRecordAt(std::size_t)` and
`recentTrailRecords(std::size_t)`. `trail()` survives for services-layer wiring and tests and is
documented as *not* a GUI entry point.

**R2 — the "no QtSql in public services headers" rule was retired as unachievable.** See the note
under §1. It is replaced by two rules that carry the same intent and can actually be grepped.

**R3 — TOOLCHAIN.md's single-TU syntax-check recipe is broken** (framework vs. `-I include/QtX`).
Corrected recipe above. The CMake path is unaffected — `find_package` resolves Qt 6.11.1 and all five
components correctly, and `cmake -B build …` reaches *Configuring done*; it currently stops at
*Generate* only because `src/**/*.cpp` do not exist yet.

**Verified and needing no change:** the virtual-base diamond is real (`StaffCustomer*` → `Person*`
converts unambiguously — a negative control with `virtual` removed from both edges fails to compile
with *"found in multiple base-class subobjects"*); `Repository<T>` instantiates for all 11 entities;
`Result<T>` for all 6 payload types; no concrete class is accidentally abstract and no polymorphic
base lacks a virtual destructor; every one of the 13 service methods named in GUI headers exists with
a matching signature; all 247 `@oop-concept` tags anchor to a real construct; and no `double` holds
currency anywhere (the only `double`s are physical quantities — `Ingredient::stockQty`,
`RecipeLine::qtyPerServing`, restock/waste amounts).

**Known cosmetic drift left for the `docs/OOP_COVERAGE.md` author:** concept names are not spelled
consistently across lanes — `Abstract Class` / `Abstract Classes` / `Abstract Class (surfaced)`,
`Pure Virtual Function` / `Pure Virtual Functions`, `STL (vector)` / `STL vector`, and a
`(surfaced)` suffix used only by the GUI lane to mean "this concept becomes visible to the user
here". Canonicalise when building the matrix; do not renumber the tags in place.

*End of frozen architecture contract.*

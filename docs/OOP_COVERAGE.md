# AluChop — OOP Syllabus Coverage Matrix

**ENCT151 Object-Oriented Programming — coursework evidence document**
Author: Shashank Bhattarai (ACE082BCT078)
Maps every concept in `SPEC.md` §5 to a **real file:line in this repository**, with the reason the
concept belongs at that spot rather than being staged for the marker.

---

## How this document was produced (and how to check it)

Every row below was verified by **reading the code at the cited line**, not by trusting the
in-source `/// @oop-concept` tag. The tags exist so the evidence is greppable:

```bash
# every tag in the codebase (440 tags across 137 files, as re-counted for this revision)
grep -rn "@oop-concept" include src

# one concept at a time
grep -rn "@oop-concept Virtual Base Class" include src
```

Line numbers were **re-verified against the tree as it now stands** (several files were
substantially rewritten after the first audit, and the citations that had drifted have been moved).
If a file is edited again, re-grep the tag rather than trusting the number.

**Honesty rule applied throughout:** a concept is marked *load-bearing* only when removing it would
break a real feature. Where a concept is present and correct but has **no production caller**, it is
marked *demonstrative* and listed again in the **GAPS** section. Nothing was stretched to claim
compliance.

---

## 1. Summary

| § | Group | Required | Present | Load-bearing | Demonstrative only |
|---|---|---|---|---|---|
| 5.1 | C++ Basics | 14 | **14 / 14** | 14 | 0 |
| 5.2 | Objects & Classes | 15 | **15 / 15** | 15 | 0 |
| 5.3 | Operator Overloading | 8 named (+ extras) | **8 / 8** (+7 extra) | 7 | 1 |
| 5.4 | Inheritance | 10 | **10 / 10** | 10 | 0 |
| 5.5 | Polymorphism | 5 | **5 / 5** | 5 | 0 |
| 5.6 | File Handling | 8 | **8 / 8** | 7 | 1 |
| 5.7 | Templates & STL | 8 | **8 / 8** | 8 | 0 |
| 5.8 | Exception Handling | 6 | **6 / 6** | 6 | 0 |
| | **Total** | **74** | **74 / 74** | **72** | **2** |

The two *demonstrative-only* items are itemised in **GAPS** (§10): the **postfix** `operator++` and
the random-access **write** `overwriteAt`. They are correct code with no current call site — they
are not missing, and they are not fake, but they should not be claimed in a viva as "the system
depends on this".

> **What changed since the first audit.** Two items that were demonstrative are now load-bearing:
> `operator<<` is what writes the archival plain-text receipt (§5.3 #34), and random-access read
> **by index** is now driven by a user-operable record browser on the Reports screen (§5.6 #60).
> Both are re-verified below and in §10.

---

## 2. §5.1 — C++ Basics

| # | Concept | File:line | Code sketch | Why it is natural here |
|---|---|---|---|---|
| 1 | Functions | `src/services/BillingService.cpp:328` | `core::Money BillingService::changeFor(core::Money total, core::Money tendered) const` | Free/member functions are the entire fabric of the codebase; this one is the cash-drawer change calculation — a pure function of two amounts, so it is a function and not a class. |
| 2 | Function overloading | `src/models/Enums.cpp:137,147,157,167,177,187,197` | `QString toString(OrderType); QString toString(OrderStatus); …` (7 overloads) | Seven unrelated enums all need "give me the DB token". Overloading means no call site ever has to invent a name like `orderStatusToString`. Also `Order::addItem` ×2 (`Order.hpp:101,109`), `Logger::log` ×2 (`Logger.hpp:73,81`), `Money::operator*` in both operand orders (`Money.hpp:185,194`). |
| 3 | Inline functions | `include/aluchop/models/Ingredient.hpp:89` | `bool isLow() const noexcept { return m_stockQty <= m_lowThreshold; }` | The dashboard evaluates this once per ingredient (157 of them) on every refresh; defining it in-class removes a call per ingredient per repaint. Also `core::formatNpr` (`Money.hpp:250`, explicit `inline`), `StaffCustomer::staffDiscountPercent` (`StaffCustomer.hpp:83`), `Admin::canManageUsers` (`Admin.hpp:59`), and the four header-only chart rules in `gui::chartkit` (`ChartKit.hpp`). |
| 4 | Default arguments | `include/aluchop/core/Money.hpp:54` | `static constexpr Money fromRupees(int64_t rupees, int64_t paisa = 0)` | Most prices in a Nepali restaurant are whole rupees, so `Money::fromRupees(450)` should just work. 30 tagged sites; the most load-bearing are `OrderService::createOrder` (`OrderService.hpp:44` — a walk-in takeaway has neither table nor customer) and `SettingsRepository::get(key, fallback)` (`SettingsRepository.hpp:23`). |
| 5 | Pass by reference | `include/aluchop/persistence/AuditTrail.hpp:70` | `bool verifyIntegrity(std::size_t& firstBadIndex);` | A `bool` can only say "the trail is damaged". The out-parameter says **which record** is the first damaged one — information a return value could not carry without inventing a struct. 19 tagged sites; `const&` parameters (`Person::setName(const QString&)`, `Person.hpp:75`) avoid copying every string in every setter. |
| 6 | Return by reference | `src/models/Order.cpp:289-298` | `OrderItem& Order::operator[](std::size_t i) { … return m_items[i]; }` | `(*order)[index].setQty(qty)` in `OrderService.cpp:248` mutates the line **in place**. `m_items` is a data member of `*this`, so the reference outlives the call — **nothing dangling**. Same for `Person::name()` (`Person.hpp:64`) returning `const QString& m_name`. `core::clampValue` (`Algorithms.hpp:71`) returns a reference to one of its *parameters*; both call sites (`Employee.cpp:110`, `NotificationService.cpp:76`) pass named constants, never temporaries, so the result is never dangling. |
| 7 | Arrays | `include/aluchop/persistence/BinaryRecordFile.hpp:45-47` | `char action[16]; char entity[16]; char details[64];` | Raw C arrays are *why* the audit record is exactly 128 bytes (`static_assert` on line 51). A `std::string` would make the record variable-length and destroy the `index * 128` offset arithmetic that random access depends on. Also `std::array<const char*,14> kMenuCategories` (`AppInfo.hpp:72`), the DDL statement array (`SchemaMigrator.cpp:65`), the 12-table seating plan (`SchemaMigrator.cpp:526`). |
| 8 | Strings | `include/aluchop/core/Exceptions.hpp:39,71` | `explicit AluChopException(const std::string& what)` … `const std::string& message() const` | `std::string` in the exception layer (so exceptions link without Qt); `QString` everywhere in models/GUI (Unicode-correct for Nepali dish names). `Money::toString()` (`Money.cpp:55`) does the grouping/decimal formatting by hand. Conversion happens at exactly one boundary: `QString::fromUtf8(e.what())`. |
| 9 | Pointers | `include/aluchop/gui/Sidebar.hpp:69` | `std::vector<QToolButton*> m_buttons;` — raw, Qt-parent-owned | Qt's parent-child ownership already deletes these; a `unique_ptr` here would double-delete. This is the one place raw pointers are *correct*, and the header says so (`Sidebar.hpp:34`). Contrast `EmployeeRepository::allTyped()` which returns owning `unique_ptr`s. |
| 10 | Dynamic memory allocation | `src/persistence/EmployeeRepository.cpp:81-92` | `e = std::make_unique<models::Waiter>(id, name, …);` (4 branches, then a `throw` for an unknown token) | The class to build is a **runtime fact read out of the `position` column** — there is no way to name the type at compile time, so the object must be heap-allocated. Also `new` for every Qt widget (`LoginWindow.cpp:93`, `MainWindow.cpp`), `std::make_unique<AppContext>` (`main.cpp:233`). |
| 11 | Structures | `include/aluchop/persistence/BinaryRecordFile.hpp:39` | `struct AuditRecord { int64_t timestampUtcMs; … uint32_t checksum; };` | Pure data with **no invariant to protect** — it is written to disk byte-for-byte, so `class` + getters would be dishonest. `static_assert(is_trivially_copyable_v<AuditRecord>)` on line 52 is the proof it stays a POD. Also `core::AppInfo` (`AppInfo.hpp:23`), `gui::Palette` (`ThemeManager.hpp:60`), `TokenRow<E>` (`Enums.cpp:35`). |
| 12 | Enumerations | `include/aluchop/models/Enums.hpp:21,31,42,49,56,64,…` | `enum class OrderStatus { Open, Pending, Preparing, Ready, Served, Paid, Cancelled };` | Seven scoped enums, and each enumerator maps to exactly one TEXT token accepted by a SQLite `CHECK` constraint. Scoped (`enum class`) so `OrderStatus::Cancelled` and `ReservationStatus::Cancelled` cannot collide. Also `Logger::Level` (`Logger.hpp:44`), `ThemeManager::Mode` (`ThemeManager.hpp:93`), `MenuSort` (`MenuService.hpp:26`), `ReportKind` (`ReportService.hpp:29`). |
| 13 | Namespaces | `include/aluchop/models/Person.hpp:11` etc. | `namespace aluchop::models { … }` | **Five** layer namespaces — `aluchop::core`, `aluchop::models`, `aluchop::persistence`, `aluchop::services`, `aluchop::gui` — are what make the layering in `docs/ARCHITECTURE.md` *enforceable by reading an include*. Nested constant namespaces `core::sage` and `core::tuning` (`AppInfo.hpp:85,103`) group design tokens; `gui::chartkit` (`ChartKit.hpp:44`) is a nested namespace used as a **vocabulary of functions** so the two chart screens cannot drift apart. File-local `namespace { … }` (`Enums.cpp:30`, `MenuService.cpp:29`, `BinaryRecordFile.cpp:33`) gives helpers internal linkage. |
| 14 | Constants | `include/aluchop/core/AppInfo.hpp:86-94` | `inline constexpr const char* kPrimary = "#5D7A66";` … | SPEC §1's palette stated **once**, so the QSS generator cannot drift from the specification. Also `kAuditRecordMagic` (`BinaryRecordFile.hpp:55`), `SchemaMigrator::kSchemaVersion`, `AuthService`'s password-policy minimum (`AuthService.hpp:101`), `CommandStack::kMaxDepth` (`Commands.hpp:138`), `tuning::kPaisaPerRupee` (`AppInfo.hpp:104`). |

---

## 3. §5.2 — Objects & Classes

| # | Concept | File:line | Code sketch | Why it is natural here |
|---|---|---|---|---|
| 15 | Multiple classes | 86 headers / 77 sources | headers: `models` (22) · `gui` (22) · `persistence` (20) · `services` (16) · `core` (6) | Not a count for its own sake: each class owns one responsibility, and the layer boundaries (§5.1 #13) are what stop a widget touching SQL. |
| 16 | Objects | `src/services/EmployeeService.cpp:293` | `models::StaffCustomer fused(guest->id(), …, hit->position(), hit->salary(), hit->shift());` | A concrete object built from two database rows and then queried for both its staff discount and its loyalty points. |
| 17 | Constructors | `src/models/Order.cpp:140` | `Order::Order() : m_createdAt(QDateTime::currentDateTimeUtc()) { ++s_openCount; }` | Registers the object in the live-order counter — construction has a real side effect. (The counter's public accessor is not yet read by any screen: see GAPS G8.) |
| 18 | Destructor | `src/persistence/BinaryRecordFile.cpp:55-62` | `~BinaryRecordFile() { if (m_stream.is_open()) { m_stream.flush(); m_stream.close(); } }` | RAII on a file handle. Deliberately **does not throw** — the comment on line 57 explains that throwing during stack unwinding calls `std::terminate`. Also `Order::~Order` (`Order.cpp`) decrements the static counter; `CsvWriter::~CsvWriter` (`CsvWriter.cpp:40`) flushes a half-written export. |
| 19 | Copy constructor | `src/models/Order.cpp:161-174` | `Order::Order(const Order& other) : m_id(0), m_orderNumber(), … m_items(other.m_items) { ++s_openCount; }` | **Split-bill is literally a copy.** `OrderService.cpp:368` does `models::Order breakaway(*original);` and then strips lines from each half. The copy resets `id`/`orderNumber` because two rows may not share a primary key — so "copy" genuinely means "a new, unsaved order carrying these lines". Also `KitchenQueue::snapshot()` (`OrderService.cpp:52`) copies the queue so displaying the pass never drains it. |
| 20 | Parameterised constructor | `include/aluchop/models/StaffCustomer.hpp:63` | `StaffCustomer(int personId, QString name, QString phone, QString email, QString position, core::Money monthlySalary, QString shift);` | One call fuses an employee row and a customer row into one identity. 22 tagged sites; every model has one and every one validates through its setters. |
| 21 | Objects as members | `include/aluchop/models/Order.hpp:237` | `std::vector<OrderItem> m_items;` | An order **contains** its lines by value. That is precisely what makes split (move lines between two `Order` values) and merge (`+=`) expressible as ordinary C++ instead of database gymnastics. Also `AppContext` (`AppContext.hpp:56`) holds every repository and service as a value member, so destruction order falls out of the language. |
| 22 | Object arrays | `include/aluchop/gui/MainWindow.hpp:116` (tag at `src/gui/MainWindow.cpp:147`) | `std::array<Page*, 9> m_pages{};` | Nine screens, nine slots, indexed by sidebar order — a fixed-size array states "there are exactly nine" in the type. Also `std::array<StatCard*,4>` (`DashboardPage.hpp:41`), `std::array<TokenRow<E>,N>` conversion tables (`Enums.cpp:80+`), 7 `Money` objects for the weekly chart (`ReportService.cpp:144`). |
| 23 | Object pointers | `src/persistence/EmployeeRepository.cpp:135` (decl `EmployeeRepository.hpp:53`) | `std::vector<std::unique_ptr<models::Employee>> allTyped() const;` | A heterogeneous staff list can only be a vector of base pointers. `EmployeeService::payrollPreview` (`EmployeeService.cpp:133`) then walks it calling `monthlyPay()` with no `if (role == …)` anywhere. |
| 24 | Dynamic objects | `src/services/ReportService.cpp:259` | `return std::make_unique<SalesReport>(m_payments, from, to);` (5 branches) | The report kind is chosen in a combo box at runtime, so the object is created on the heap and handed back through the abstract `ReportGenerator*`. |
| 25 | Static members | `src/models/Order.cpp:129` | `int Order::s_openCount = 0;` | One counter shared by every `Order` in the process. **Contract enforced in code:** *every* constructor (default, parameterised, copy, move) increments and the destructor decrements exactly once — incrementing only in the default constructor would drive the count negative on the first split. Also `Logger::instance()` and `Database::instance()` (Meyers singletons), `ThemeManager::instance()`, and `AuthService::hashPassword` (`AuthService.cpp:127`, a static pure function needing no object) — those four are load-bearing; the `openOrderCount()` accessor itself is not yet called (GAPS G8). |
| 26 | Constant objects | `src/models/Chef.cpp:21` | `const core::Money Chef::kOvertimeRatePerHour = core::Money::fromRupees(300);` | One immutable house rate shared by every `Chef`, defined once instead of repeated in payroll code. Also `const Palette ThemeManager::kLight` / `kDark` (`ThemeManager.cpp:65,87` — SPEC §1 verbatim), `inline const AppInfo kAppInfo` (`AppInfo.hpp:38`), and every repository held as `const persistence::PaymentRepository&` inside the report classes (`ReportGenerator.hpp:70`) — a const object on which only const members can be called. |
| 27 | Constant member functions | `include/aluchop/models/Bill.hpp:100` | `core::Money total() const noexcept { return m_subtotal - m_discount + m_serviceCharge; }` | The total is *derived*, never stored twice, so it cannot drift from its parts — and `const` is what lets a `const Bill&` be printed. 15 tagged sites. The subtlest is `SettingsService::get(...) const` (`SettingsService.cpp:52`) whose memo cache is `mutable`: reading a setting is *logically* const even though it warms a cache. |
| 28 | Friend functions | `include/aluchop/core/Money.hpp:147` | `friend std::ostream& operator<<(std::ostream& os, const Money& m);` | The stream must be the **left-hand** operand, so it cannot be a member; it still needs the private `m_paisa`, which is exactly what `friend` is for. Also `friend bool operator==(const MenuItem&, const MenuItem&)` (`MenuItem.hpp:96`) — written as a free function so neither operand is privileged, keeping equality symmetric. |
| 29 | Friend classes | `include/aluchop/models/Bill.hpp:44` | `friend class aluchop::services::BillingService;` | `Bill::settle()` is **private**. `BillingService.cpp:256` is therefore the *only* line in the entire application where a bill can become "paid", and it runs after the payment row is committed. This is friendship used to **narrow** access, not widen it — without it, `settle()` would have to be public and any widget could mark a bill paid with no `Payment` row ever reaching the database. |

---

## 4. §5.3 — Operator Overloading

| # | Operator | File:line | Code sketch | Why it is natural here |
|---|---|---|---|---|
| 30 | `+` | `include/aluchop/core/Money.hpp:162` | `constexpr Money operator+(Money a, Money b) noexcept { return Money(a.paisa() + b.paisa()); }` | Money is a value type. `salary() + m_bonus` (`Manager.cpp:39`) and `salary() + m_tips` (`Waiter.cpp:40`) are the payroll rules written in the notation the domain uses. Integer paisa throughout — no floating-point drift. |
| 31 | `-` | `include/aluchop/core/Money.hpp:170,177` | `operator-(Money,Money)` and unary `operator-(Money)` | `tendered - total` **is** change (`BillingService.cpp:331`); unary minus expresses refunds/credits. |
| 32 | `==` | `include/aluchop/models/MenuItem.hpp:96` | `friend bool operator==(const MenuItem& a, const MenuItem& b) { return a.m_id == b.m_id; }` | Identity is the **database row**, not the name: renaming "Chicken Momo" to "Chicken Mo:Mo" must not turn it into a different dish inside an open order. Also `operator==`/`!=` on `Money` (`Money.hpp:202,210`). |
| 33 | `<` | `src/models/MenuItem.cpp:144` | `bool MenuItem::operator<(const MenuItem& rhs) const` | The default menu sort. Used at `MenuService.cpp:58` (`return a < b;`) driving `std::sort`. Falls back name → category → id so it is a **strict weak ordering**, which `std::sort` requires — two dishes may legitimately share a name across categories. Also `Money::operator<` (`Money.hpp:218`) makes `Money` sortable and usable as a map key. |
| 34 | `<<` | `src/models/Bill.cpp:247` · `src/core/Money.cpp:85` — **called from** `src/services/BillingService.cpp:86` | `std::ostream& operator<<(std::ostream& os, const Bill& b)` … `std::ostringstream out; out << bill;` | Streams the whole receipt — lines, discount, promo, service, tax-inclusive note — into any `std::ostream`, and every amount on it goes out through `core::operator<<(std::ostream&, const Money&)`. **Load-bearing:** `BillingService::settle()` streams the settled `Bill` into a `std::ostringstream` (`streamedReceipt`, `BillingService.cpp:86-90`) and appends the result to the raw `<fstream>` application log (`BillingService.cpp:301-304`), so every receipt this till issues survives as plain text outside SQLite. The `friend` declaration is what lets the operator read `Bill`'s private snapshot rather than a recomputed total. |
| 35 | `[]` | `src/models/Order.cpp:289-298` | `OrderItem& Order::operator[](std::size_t i)` + `const` overload | An order **is** a sequence of lines, so `order[2].setQty(3)` is the natural notation — used at `OrderService.cpp:248` and `Commands.cpp:47`. Unlike the raw `std::vector` subscript this one is **bounds-checked and throws**, because the index arrives from a GUI table selection. |
| 36 | `=` (assignment) | `include/aluchop/models/Order.hpp:70` · `src/models/Order.cpp:176` | `Order& Order::operator=(const Order& other)` — self-assignment guarded, resets id/number | Same deep-copy semantics as the copy constructor. It exists because `Order` has a user-declared copy constructor: leaving assignment implicit would give copy and assignment **different** semantics. Paired with `noexcept` move ctor/assign (`Order.hpp:82,87`) — the move deliberately **preserves** identity, because `std::vector<Order>` relocation must not silently reset the id of every order loaded from SQLite. |
| 37 | `++` | `src/models/Customer.cpp:65,73` | `Customer& operator++();` and `Customer operator++(int);` | `++customer` **is** "one more visit". Used at `CustomerService.cpp:234` (`++(*guest);`). The alternative — a public `setVisits(int)` — would let any caller set the count to anything. The postfix form delegates to the prefix form so there is one increment rule, not two. *Postfix has no current call site (GAPS G2).* |
| 38 | Extras | `Money.hpp:112,122,132,185,194,210,226,234,242` · `Result.hpp:75` | `+= -= *= *` (both orders) `!= <= > >=`, and `explicit operator bool()` on `Result<T>` | `bill += line` / `total += revenue` appear throughout billing and reporting. `explicit operator bool` lets `if (auto r = service.doThing())` read naturally while stopping a `Result` decaying to a number. |

---

## 5. §5.4 — Inheritance

The people hierarchy is the load-bearing one:

```
                    Person            (abstract; VIRTUAL base — one id/name/phone/email)
                   /      \
          virtual /        \ virtual
             Employee     Customer
             /  |  \           \
       Waiter Chef Manager      \
                     |           \
                   Admin ────── StaffCustomer
                     |
                 IAuditable
```

| # | Form | File:line | Code sketch | Why it is natural here |
|---|---|---|---|---|
| 39 | Single | `include/aluchop/models/Manager.hpp:24` | `class Manager : public Employee` | Manager extends exactly one concrete base and adds one piece of state (the bonus). Also `ElegantTable : QTableWidget` (`Widgets.hpp:105`) and `SplashScreen : QSplashScreen` (`SplashScreen.hpp:41`) — pure specialisation. |
| 40 | Multiple | `include/aluchop/models/Bill.hpp:37` | `class Bill : public IPrintable, public IDiscountable` | Printing and discounting are genuinely independent capabilities; keeping them as separate stateless interfaces is what makes this honest rather than decorative. Also `Admin : public Manager, public IAuditable` (`Admin.hpp:31`) — a concrete role base **plus** a capability mixin. |
| 41 | Hierarchical | `Waiter.hpp:22` · `Chef.hpp:20` · `Manager.hpp:24` | Three independent siblings specialise `Employee` | Each overrides `monthlyPay()` with a **genuinely different rule** (tips / overtime × house rate / fixed bonus), so a chef and a waiter on the same base salary take home different amounts. Also five report classes over `ReportGenerator` (`ReportGenerator.hpp:63-119`) and nine screens over `Page` (`Page.hpp:38`). |
| 42 | Multilevel | `include/aluchop/models/Admin.hpp:31` | `Person → Employee → Manager → Admin` | Each level adds real state: identity → payroll → bonus → privilege. `Admin` deliberately does **not** override `monthlyPay()` — an admin is paid exactly as a manager is, and inheriting the rule rather than copying it is the honest modelling choice (`Admin.cpp:12-14`). |
| 43 | Hybrid | `include/aluchop/models/StaffCustomer.hpp:49` | `class StaffCustomer : public Employee, public Customer` | Multiple inheritance layered on top of a chain rooted in a **virtual base** — the diamond. `Admin` is the other hybrid: multilevel + interface, over the same virtual base. |
| 44 | Public | `include/aluchop/persistence/UserRepository.hpp:25` | `class UserRepository : public Repository<models::User>` | A `UserRepository` genuinely **IS-A** `Repository<User>` — callers may hold it as the base and call `findAll()`/`findById()`. |
| 45 | Protected | `include/aluchop/services/ReportGenerator.hpp:35` | `class ReportGenerator : protected persistence::CsvWriter` | A report is **implemented-in-terms-of** a CSV writer, not a kind of one. `protected` (not `private`) because the derived report classes are also part of the implementation. Outside callers can reach `exportCsv()/title()/header()/rows()` — but **never** `writeRow()` directly, so no caller can emit a body row without a header. Also `protected core::Money m_bonus` in `Manager.hpp:57`, reachable by `Admin` while code outside the hierarchy must go through the validating setter. |
| 46 | Private | `include/aluchop/persistence/AuditTrail.hpp:31` | `class AuditTrail : private BinaryRecordFile` | The base can write **any** 128 bytes **anywhere**. The trail may not: it guarantees (a) `seq` is 1-based and strictly increasing, (b) every record carries a checksum computed at write time. Private inheritance makes `append()` and `overwriteAt()` unreachable from outside, so the **only** way a record reaches the file is `record()`, which maintains both invariants. `using BinaryRecordFile::close;` on line 73 selectively re-exports the one verb that cannot break them. Make this base public and the tamper-evidence guarantee evaporates. |
| 47 | **Virtual base class** | `Employee.hpp:37` · `Customer.hpp:23` · `StaffCustomer.hpp:49` | `class Employee : virtual public Person` / `class Customer : virtual public Person` | **See the proof in §9.1.** A staff member enrolled in the loyalty programme is one human with one name, one phone and one id, who appears in two tables. Without `virtual` this object carries **two** `Person` subobjects: `sc.name()` becomes "found in multiple base-class subobjects" and *does not compile*; `sc.setId(7)` would update only one half; the halves could drift. |
| 48 | Method overriding | `src/models/Waiter.cpp:35` · `Chef.cpp:39` · `Manager.cpp:37` | `core::Money Waiter::monthlyPay() const { return salary() + m_tips; }` | 23 tagged sites. `EmployeeService::payrollPreview` (`EmployeeService.cpp:139`) calls `person->monthlyPay()` and never asks what it is holding — that is the whole point. `AluChopException::what()` (`Exceptions.hpp:65`) is overridden so context and code travel with the throw. `Admin::roleName() const **final**` (`Admin.hpp:50`) stops the privilege chain: no future subclass can inherit administrative rights and then report a weaker-sounding role name. |

---

## 6. §5.5 — Polymorphism

| # | Concept | File:line | Code sketch | Why it is natural here |
|---|---|---|---|---|
| 49 | Virtual functions | `include/aluchop/models/Employee.hpp:63` | `virtual core::Money monthlyPay() const;` | A sensible default (base salary) that each role refines — which is why the payroll loop needs no `if (role == …)` chain anywhere. Also `Person::displayLabel()` (`Person.hpp:54`), `Repository<T>::orderByClause()` (`Repository.hpp:92`). |
| 50 | Virtual destructors | `include/aluchop/models/Person.hpp:44` | `virtual ~Person() = default;` | People are always held as `Person*` / `unique_ptr<Employee>`, so destruction must dispatch to the real type. Every interface declares one too (`Interfaces.hpp:32,45,64,78`); `Repository<T>` (`Repository.hpp:44`); `Order::~Order() override` (`Order.hpp:94`) overrides `IPrintable`'s. |
| 51 | Pure virtual functions | `include/aluchop/models/Person.hpp:48` | `virtual QString roleName() const = 0;` | Forcing every concrete type to name its own role is what makes `"Ramesh (Waiter)"` work through a single base pointer. Also `Repository<T>::fromRecord` (`Repository.hpp:88` — hydration is the *only* entity-specific step), `ReportGenerator::header()/rows()` (`ReportGenerator.hpp:46,49`), `Page::pageTitle()/refresh()` (`Page.hpp:50,55`), `Command::execute()/undo()` (`Commands.hpp:39,42`). |
| 52 | Abstract classes | `include/aluchop/models/Interfaces.hpp:29,43,62,76` | `class IPrintable { public: virtual ~IPrintable() = default; virtual QString toPrintableText() const = 0; };` | Four **stateless** capability mixins — no data members, no constructors — so they can be combined freely without creating a diamond of data. `Person` (`Person.hpp:28`) is abstract because nobody is "just a Person" in a restaurant; `ReportGenerator` (`ReportGenerator.hpp:35`) because there is no such thing as a generic report to instantiate; `Command` (`Commands.hpp:33`) because a bare command means nothing. |
| 53 | Runtime polymorphism | `src/services/EmployeeService.cpp:133-140` | `for (const std::unique_ptr<models::Employee>& person : roster) preview.emplace_back(person->displayLabel(), person->roleName(), person->monthlyPay());` | Three columns, all from virtual dispatch — `displayLabel()`/`roleName()` from `Person`, `monthlyPay()` from the concrete role. **This loop never asks what it is holding.** The second-best example is `ReportGenerator::exportCsv` (`ReportGenerator.cpp:67`): one algorithm — open, write header, write every row, close — producing five different files because `header()` and `rows()` dispatch. |
| 54 | Compile-time polymorphism | `src/models/Enums.cpp:49,67` + `:137,147,…` | `template <typename E, size_t N> QString tokenOf(const std::array<TokenRow<E>,N>&, E, const char*)` — instantiated 7×, reached through 7 `toString` **overloads** | The clearest contrast with #53 in the codebase: the compiler picks the table by **overload resolution** and stamps out the loop by **template instantiation** — no vtable, no runtime cost, resolved entirely at compile time. Also `Result<T>::valueOr` (`Result.hpp:121-124`, a member template adapting to whatever fallback the caller has) and `Money::operator*` in both operand orders (`Money.hpp:185,194`). |

---

## 7. §5.6 — File Handling

> SQLite is **not** counted here. Every row below is raw `<fstream>`.

| # | Concept | File:line | Code sketch | Why it is natural here |
|---|---|---|---|---|
| 55 | Read | `src/persistence/BinaryRecordFile.cpp:191` | `m_stream.read(reinterpret_cast<char*>(&rec), sizeof(AuditRecord));` | Reached from `AuditTrail::at()`, `AuditTrail::tail()` and the constructor's "resume the sequence number" logic (`AuditTrail.cpp:39`), which reads the **last** record to find where numbering left off. |
| 56 | Write | `src/persistence/CsvWriter.cpp:94` | `m_out.write(utf8.constData(), utf8.size());` | Every CSV export in the Reports screen goes through this line. |
| 57 | Append | `src/core/Logger.cpp:64` | `stream.open(filePath.toStdString(), std::ios::out \| std::ios::app);` | An application log **must** be append-only: `std::ios::app` guarantees writes land at end-of-file even if another process is writing. The live log is at `~/Library/Application Support/AluChop/AluChop/logs/aluchop.log`. |
| 58 | Binary files | `include/aluchop/persistence/BinaryRecordFile.hpp:39-52` + `src/…:84` | `struct AuditRecord {…}; static_assert(sizeof(AuditRecord)==128); … m_stream.open(native, in\|out\|binary);` | The audit trail is a fixed 128-byte POD record written byte-for-byte. `in\|out\|binary` on one handle is what allows the same stream to append at the end **and** rewrite a record in the middle. |
| 59 | ASCII files | `src/persistence/CsvWriter.cpp:67` + `src/core/Logger.cpp:64` | `m_out.open(path, std::ios::out \| std::ios::trunc);` (text mode) | CSV exports and the application log are plain text a marker can open in any editor. Cell escaping lives in `CsvWriter::escapeCell`. |
| 60 | **Random access** | `src/persistence/BinaryRecordFile.cpp:185` (get) · `:225` (put) | `m_stream.seekg(index * sizeof(AuditRecord), std::ios::beg);` / `m_stream.seekp(index * sizeof(AuditRecord), std::ios::beg);` | **See the arithmetic proof in §9.4.** Record *n* lives at byte offset `n × 128` — no index, no scanning. The `seekg` half is fully load-bearing **and user-operable**: `AuditTrail::at()`/`tail()` and the constructor's sequence-resume use it, and the Reports screen's *"jump straight to a record"* browser (`src/gui/ReportsPage.cpp:1362`, via `services::AuditService::trailRecordAt`) lets a marker type any index and read that record back. `overwriteAt` (`seekp`) is correct but still has **no production caller** — GAPS G3. |
| 61 | Sequential access | `src/persistence/AuditTrail.cpp:90-92` · `src/services/ReportGenerator.cpp:74-76` | `for (size_t i = first; i < total; ++i) out.push_back(readAt(i));` / `for (const QStringList& row : body) writeRow(row);` | The audit *tail* and every CSV export walk their file front-to-back one record/row at a time — the honest contrast to #60. |
| 62 | Error checking | `src/persistence/BinaryRecordFile.cpp` (whole TU) · `BackupManager.cpp:182-196` | After **every** `open`/`seekg`/`seekp`/`read`/`write`/`flush`: `if (m_stream.fail()) failIo(op, path);` → `throw core::FileIOException(msg, path, errno)` | The binding rule of that translation unit, stated in its file comment: **no stream operation goes unchecked**, and every failure names the operation, the path and `errno`. On top of that, `readAt` verifies the record's magic signature *and* its checksum before returning it (`:197-206`), so a tampered trail fails loudly. `BackupManager::isValidSqliteFile` reads the 16-byte header with a binary `ifstream` and checks `is_open`, `good`, `gcount` and `bad` **before** any destructive restore step runs. |

---

## 8. §5.7 — Templates & STL · §5.8 — Exception Handling

### §5.7 Templates & STL

| # | Concept | File:line | Code sketch | Why it is natural here |
|---|---|---|---|---|
| 63 | Function templates | `include/aluchop/core/Algorithms.hpp:33` | `template <typename Container, typename Projection> Money sumMoney(const Container& c, Projection proj)` | The same summation totals an order's lines (`Order.cpp:329`), a day's payments (`ReportGenerator.cpp:128-129`) and a dashboard metric. Writing it once is what stops the same loop being copy-pasted into five services. Also `countMatching` and `clampValue` (`Algorithms.hpp:51,71`), and `tokenOf`/`valueOf` over 7 enums (`Enums.cpp:49,67`). |
| 64 | Class templates | `include/aluchop/persistence/Repository.hpp:37` | `template <typename T> class Repository { std::vector<T> findAll() const; … virtual T fromRecord(const QSqlRecord&) const = 0; };` | `findAll`/`findById`/`count`/`removeById` are *identical* for every id-keyed table; only hydration differs, so it is the one pure virtual hook. A class template **with** a pure virtual — generic *and* abstract. Also `core::Result<T>` (`Result.hpp:33`), the success-or-error carrier returned across every service boundary, **with a full specialisation `Result<void>`** (`Result.hpp:143`) because `void` has no storage the primary template could hold. |
| 65 | STL (general) | throughout | `std::optional`, `std::unique_ptr`, `std::array`, `std::pair`, `std::tuple`, `std::set`, `std::function` | `std::optional<T>` is the repository's "no such row"; `unique_ptr` is the only ownership in the polymorphic staff list; `std::set<size_t>` de-duplicates the split-bill line selection (`OrderService.cpp:358`). |
| 66 | `vector` | `include/aluchop/persistence/Repository.hpp:51` | `std::vector<T> findAll() const` | Every result set in the application. Also `std::vector<OrderItem>` inside `Order`, `std::vector<unique_ptr<Command>>` for the undo/redo stacks (`Commands.hpp:107`). |
| 67 | `map` | `src/services/CustomerService.cpp:116` | `std::map<QString,int> tally; … tally[line.name()] += line.qty();` | "Favourite orders" is a tally across a guest's history — dish name → total quantity. Ordered iteration makes the tie-break deterministic. Also `SettingsService`'s `mutable std::map` settings cache (`SettingsService.cpp:53`). |
| 68 | `queue` | `include/aluchop/services/KitchenQueue.hpp:26,64` | `std::queue<int> m_q;` | A real kitchen works in the order tickets arrive — the container's FIFO guarantee **is** the domain rule, so no hand-rolled queue is needed. Rebuilt from persisted Pending orders at start-up (`AppContext`). |
| 69 | Algorithms | `src/services/MenuService.cpp:130,138` · `CustomerService.cpp:126` · `OrderService.cpp:69` | `std::copy_if` → `std::sort` → `std::partial_sort` → `std::find_if` | The menu screen's filter+sort is `copy_if` then `sort` with a per-mode comparator, so adding a sort option never means adding another SQL query. `partial_sort` for "top N favourites" — only the top N are wanted. `std::sort` ×15, `std::find_if` ×10, `std::copy_if` ×2, `std::remove_if` ×1 across the tree. |
| 70 | Iterators | `include/aluchop/core/Algorithms.hpp:35` · `src/services/OrderService.cpp:69-76` | `for (auto it = c.begin(); it != c.end(); ++it) total += proj(*it);` / rebuild the queue skipping one iterator position | `sumMoney` uses explicit iterators rather than range-for so it works for **any** conforming container. `KitchenQueue::remove` finds an iterator with `find_if` and rebuilds the queue skipping exactly that position — cancelling one ticket without disturbing the order of the rest. |

### §5.8 Exception Handling

| # | Concept | File:line | Code sketch | Why it is natural here |
|---|---|---|---|---|
| 71 | `throw` | `src/services/InventoryService.cpp:257` | `throw core::InventoryException(...)` | An inconsistent recipe is an exceptional condition, not a return code. Model setters throw `ValidationException` (`Person.cpp`, `Employee.cpp`, `Manager.cpp:45`), so an invariant cannot be violated through *any* code path. |
| 72 | `try` / `catch` | `src/services/AuthService.cpp:146-197` | `try { … throw core::AuthException(...); } catch (const core::AluChopException& e) { return R::err(...); }` | **The architectural rule:** persistence *throws*; services catch **once, at their own boundary**, and convert to `core::Result<T>`. That is why no widget slot in the whole GUI contains a `try` for business failures. |
| 73 | Multiple catch | `src/main.cpp:236,243,250,257` and `:305,309,313,317` | `catch (const DatabaseException&) … catch (const FileIOException&) … catch (const ValidationException&) … catch (const AluChopException&) … catch (const std::exception&) … catch (...)` | Four different start-up faults get four genuinely different explanations to the user ("the database could not be opened" vs "the menu seed file is malformed"), then `std::exception` and a catch-all close the door on everything else. `OrderService.cpp:533` / `:540` (tagged at `:507`) is the best domain example: an `InventoryException` while serving means the store room disagrees with the recipe book — the plate is already on the guest's table, so the recovery is a **loud warning that still lets the food leave the pass**, whereas a `DatabaseException` is returned as a hard error. Two failure kinds, two genuinely different recoveries. |
| 74 | **Rethrow** | `src/persistence/Database.cpp:252-259` · `src/services/AuditService.cpp:128` · `src/services/ReportGenerator.cpp:87` | `catch (...) { db.rollback(); throw; }` | **Bare `throw;` — see §9.3.** Three sites, each for the same reason: perform a cleanup the caller cannot, then let the **original** exception continue untouched. The transaction wrapper must not substitute an invented "transaction failed" for the real cause. |
| 75 | Custom exceptions | `include/aluchop/core/Exceptions.hpp:33,128,155,187,213,240` | `AluChopException : std::runtime_error` → `DatabaseException`, `ValidationException`, `AuthException`, `InventoryException`, `FileIOException` | Exactly the hierarchy SPEC §5.8 asks for. Deriving from `std::runtime_error` means generic `catch (const std::exception&)` handlers still work, while `catch (const AluChopException&)` isolates *our* failures from everyone else's. Each child carries **context** (offending field / file path / SQL / username) and a numeric code, folded into `what()` by an overridden `what()` (`:65`) and a virtual `category()` (`:90`) — so one handler prints the right subsystem for any caught child. |

---

## 9. Proofs for the four trickiest items

### 9.1 Virtual base class — what actually breaks without `virtual`

**Declarations:** `Employee.hpp:37` `class Employee : virtual public Person` · `Customer.hpp:23` `class Customer : virtual public Person` · `StaffCustomer.hpp:49` `class StaffCustomer : public Employee, public Customer`

**The real situation being modelled:** staff eat at the restaurant, they get a staff discount, and
management still wants their spend on the loyalty ledger. `EmployeeService::staffCustomerFor()`
(`EmployeeService.cpp:272-312`) fuses a customer row with an employee row when the phone numbers
match; `BillingService` then offers the 10 % staff discount as one of the competing discount
candidates.

**Remove `virtual` from the two edges and:**

1. A `StaffCustomer` contains **two** `Person` subobjects.
2. `sc.name()` no longer compiles — *"member found in multiple base-class subobjects of type Person"*.
   Every unqualified access needs `sc.Employee::name()` or `sc.Customer::name()`.
3. `sc.setId(7)` updates only one half. The employee identity and the customer identity can silently
   drift apart — the exact bug the class exists to prevent.
4. `StaffCustomer::displayLabel()` (`StaffCustomer.hpp:79`), which reaches `name()` from the Person
   half and `loyaltyPoints()` from the Customer half in one expression, becomes meaningless: *which*
   name?

**The constructor consequence, which is the part markers probe:** a virtual base is initialised by
the **most-derived** class *only*. So `StaffCustomer.cpp:37-41` reads

```cpp
StaffCustomer::StaffCustomer(int personId, QString name, QString phone, QString email,
                             QString position, core::Money monthlySalary, QString shift)
    : Person(personId, name, phone, email),                       // ← the most-derived class does this
      Employee(personId, name, phone, email, std::move(position), monthlySalary, std::move(shift)),
      Customer(personId, name, phone, email)
```

The `Person(...)` calls written *inside* `Employee`'s and `Customer`'s own constructors are **skipped
entirely** for this object. If `StaffCustomer` did not name `Person` itself, the identity would be
default-constructed and every field would come out blank. The same rule applies at every level:
`Waiter.cpp:23`, `Chef.cpp:27`, `Manager.cpp:25` and `Admin.cpp:32` each name `Person(id, name, phone,
email)` in their own initialiser list — `Admin` doing so **three levels** below `Person`, because it
is the most-derived class.

### 9.2 Private and protected inheritance — the implemented-in-terms-of rationale

**Private — `AuditTrail : private BinaryRecordFile` (`AuditTrail.hpp:31`)**

`BinaryRecordFile` can write **any** 128 bytes at **any** offset (`append`, `overwriteAt`).
`AuditTrail` layers on two invariants the base knows nothing about:

1. `seq` is 1-based and increases by exactly one per record (resumed from the last record at
   construction, `AuditTrail.cpp:35-47`);
2. every stored record carries a checksum computed at write time (`AuditTrail.cpp:63`).

Private inheritance is what **enforces** them: `append()` and `overwriteAt()` are unreachable from
outside, so the only route to disk is `record()`, which maintains both. Line 73 selectively
re-exports the one safe verb — `using BinaryRecordFile::close;`. Make the base public and the
tamper-evidence guarantee is gone: any caller could `append()` a hand-built record with a fake
sequence number.

**Protected — `ReportGenerator : protected persistence::CsvWriter` (`ReportGenerator.hpp:35`)**

A report **is not** a CSV writer — it is a title, a header row and freshly computed rows. But its
five derived report classes are part of the same implementation, so they need the writer verbs;
`private` would hide them from the subclasses, `public` would let any caller call `writeRow()` and
emit a body row with no header. `protected` is the exact access level the design requires.
The template method `exportCsv` (`ReportGenerator.cpp:67-89`) uses `open` / `writeRow` / `close`
internally while exposing none of them.

### 9.3 Rethrow — the bare `throw;`

```cpp
// src/persistence/Database.cpp:250-259
try {
    DepthGuard guard(s_depth);
    body();
} catch (...) {
    db.rollback();
    throw;              // ← bare rethrow: the ORIGINAL exception continues, untouched
}
```

`throw;` (no operand) re-raises the exception currently being handled, preserving its **dynamic
type** and its message. `throw e;` would slice a `DatabaseException` caught as `AluChopException&`
back down to the base and destroy the context and error code. The transaction wrapper's job is to
undo half-finished work — it has no business substituting an invented "transaction failed" for the
real cause the caller needs to see.

The other two are the same shape for the same reason:
- `AuditService.cpp:128` — log the failure, mirror the attempt into SQLite with sequence 0 (the
  signature of "the binary trail refused this record"), then `throw;`. **An audit trail that fails
  silently is worse than none.**
- `ReportGenerator.cpp:87` — release the file handle so a half-written export does not leak it, then
  `throw;`. The nested `try { close(); } catch (...) {}` ensures a second failure while closing
  cannot mask the first.

### 9.4 Random-access file I/O — the offset arithmetic

```cpp
// include/aluchop/persistence/BinaryRecordFile.hpp:39-52
struct AuditRecord {
    int64_t timestampUtcMs;  //   0 .. 7
    int64_t amountPaisa;     //   8 .. 15
    uint32_t magic;          //  16 .. 19
    uint32_t seq;            //  20 .. 23
    uint32_t userId;         //  24 .. 27
    char action[16];         //  28 .. 43
    char entity[16];         //  44 .. 59
    char details[64];        //  60 .. 123
    uint32_t checksum;       // 124 .. 127
};
static_assert(sizeof(AuditRecord) == 128, "AuditRecord must be exactly 128 bytes");
static_assert(std::is_trivially_copyable_v<AuditRecord>, "AuditRecord must be a POD");
```

Field order is chosen so there is **zero padding** (8-byte fields first, then 4-byte, then char
arrays), and both facts are asserted at compile time — a padded record would silently corrupt every
offset computation in the class. `BinaryRecordFile.cpp:36-39` additionally asserts the checksum sits
at offset **124**, which is what lets the checksum cover bytes `[0,124)` and still be written last.

Given a fixed 128-byte record, record *n* lives at byte offset `n × 128`:

```cpp
// read  — src/persistence/BinaryRecordFile.cpp:185
m_stream.seekg(static_cast<std::streamoff>(index * sizeof(AuditRecord)), std::ios::beg);
// write — src/persistence/BinaryRecordFile.cpp:225
m_stream.seekp(static_cast<std::streamoff>(index * sizeof(AuditRecord)), std::ios::beg);
```

No index file, no scanning: **one seek reaches any record.** `recordCount()` (`:139-147`) is the
inverse — `seekg(0, ios::end)` then `tellg() / sizeof(AuditRecord)`. Because `overwriteAt` writes
exactly `sizeof(AuditRecord)` bytes at an in-range offset, the file length never changes.

**Where a marker can see it happen.** Reports → *Verify audit trail* opens a dialog whose
*"Jump straight to a record"* panel takes any index in range and reads that record back
(`src/gui/ReportsPage.cpp:1362`, calling `services::AuditService::trailRecordAt`, which forwards to
`AuditTrail::at()` → `readAt()` → the `seekg` above). Record 0 and record 90 000 cost the same one
seek. The GUI never names a persistence type to do it — `auto` holds the returned record, keeping
the layer rule in `docs/ARCHITECTURE.md` intact.

Contrast with **sequential** access at `AuditTrail.cpp:88-92`, which walks `first → total` one record
at a time to build the "last N records" view shown underneath.

---

## 10. GAPS — honest findings

Every §5 concept is **present and correct**. The list below is what remains weaker than the
surrounding code. None is a compile or behaviour defect; all are *evidence-quality* issues a marker
could reasonably probe. The G-numbers are kept stable from the first audit so the two revisions can
be compared; four of the seven original findings have since been closed by real code, and each
closure is stated with the line that closes it so a reader can check rather than take it on trust.

| ID | Finding | Status |
|---|---|---|
| G1 | `operator<<` for `Bill`/`Money` had no call site | **CLOSED** — it now writes the archival receipt |
| G2 | postfix `Customer::operator++(int)` has no call site | **OPEN** |
| G3 | `BinaryRecordFile::overwriteAt` (`seekp` write) has no call site | **OPEN — deliberately** |
| G4 | a comment overclaimed a Reports record browser | **CLOSED** — the browser was built |
| G5 | `Namespaces` and `Strings` had no `@oop-concept` tag | **PARTLY CLOSED** — `Namespaces` now tagged, `Strings` still not |
| G6 | only one friend *class* exists | **OPEN** |
| G7 | `tests/` was empty | **CLOSED** — 372 assertions now live there |
| G8 | two public counters are implemented but never read | **OPEN** (new finding) |

---

**G1 — CLOSED. `operator<<` is now the archival receipt path.**
`src/models/Bill.cpp:247` and `src/core/Money.cpp:85` are unchanged, but they are no longer
uncalled. `BillingService.cpp` now `#include <sstream>` (line 32) and defines
`streamedReceipt(const models::Bill&)` (`:86-90`), whose entire body is `std::ostringstream out;
out << bill; return out.str();`. `BillingService::settle()` calls it at `:301-304` and appends the
result to the append-mode `<fstream>` application log, so every settled bill leaves a plain-text
copy outside SQLite. The guest-facing receipt is still `IPrintable::toPrintableText()`
(`receiptText`, `:334`) — the two render the *same* snapshot figures, and neither recomputes money.
`<<` is therefore load-bearing, and the `friend` declaration earns its place: the operator reads
`Bill`'s private snapshot directly.

**G2 — OPEN. Postfix `Customer::operator++(int)` has no call site.**
`src/models/Customer.cpp:73`. The prefix form is genuinely used (`CustomerService.cpp:234`,
`++(*guest)`), and the postfix form correctly delegates to it so there is one increment rule rather
than two. The postfix overload exists so `++` behaves like the built-in, which is defensible
design — but no code takes the pre-increment value. **This is one of the two demonstrative-only
items in the §1 table.**

**G3 — OPEN, and deliberately so. `BinaryRecordFile::overwriteAt` (the `seekp` random-access
*write*) has no call site.** `src/persistence/BinaryRecordFile.cpp:210-237`. Random-access
*reading* is now doubly load-bearing — `readAt` drives `AuditTrail::at()`, `AuditTrail::tail()`, the
constructor's sequence-resume logic **and** the Reports record browser (see G4). Random-access
*writing* is called by nothing, because an audit trail that can be rewritten in place is not an
audit trail: `AuditTrail : private BinaryRecordFile` (§9.2) is precisely what makes `overwriteAt`
unreachable. So this is a designed dead end, not an oversight — but it is honest to record that the
`seekp` half of SPEC §5.6's "random access" is a *capability*, not a used feature. **This is the
second of the two demonstrative-only items.**

**G4 — CLOSED. The Reports record browser exists.**
The old comment in `AuditService.cpp` claimed random-access-by-index was "surfaced all the way to
the Reports screen as a record browser" when `trailRecordAt` had no caller. It now has one:
`src/gui/ReportsPage.cpp` builds a *"Jump straight to a record"* panel inside the audit-integrity
dialog — a range-clamped `QSpinBox` (`#  N  of LAST`) plus a **Read this record** button whose
handler calls `m_ctx.audit().trailRecordAt(...)` at `ReportsPage.cpp:1362` and prints the record's
action, entity, sequence, amount, timestamp and its byte offset. The sequential view underneath it
still uses `recentTrailRecords(...)` (`:1454`) and the integrity walk still uses
`verifyTrailIntegrity(...)` (`:1206`), so the screen now demonstrates random access **and**
sequential access side by side. The GUI reaches all three through `services::AuditService` only and
holds the returned record in `auto`, so no persistence type is named in a GUI translation unit.

**G5 — PARTLY CLOSED. `Namespaces` is now tagged; `Strings` still is not.**
`grep -rn "@oop-concept Namespaces" include src` now returns `include/aluchop/gui/ChartKit.hpp:26`,
where `gui::chartkit` is a namespace used as a vocabulary of chart rules. There is still **no**
`@oop-concept Strings` tag anywhere: a marker grepping the tags alone would not find the string
work, even though it is unambiguously present (`std::string` throughout `core/Exceptions.hpp` so the
exception layer links without Qt; `QString` everywhere in models and GUI for Unicode-correct Nepali
dish names; the hand-rolled grouping in `Money::toString()`, `src/core/Money.cpp:55`). This is a
documentation gap, not a coverage gap — row 8 of §2 cites the code directly.

**G6 — OPEN. Only one friend *class* exists.**
`services::BillingService` is `Bill`'s sole friend (`Bill.hpp:44`); `grep -rn "friend class"
include src` returns exactly that one line. SPEC §5.2 says "friend classes" in the plural. One is
genuinely enough — and it is an *excellent* example, because friendship is used to **narrow** access
rather than widen it — but if a marker insists on plurality, this is the item to point at. (Two
friend *functions* also exist: `operator<<` for `Money` and `Bill`, and `operator==`/`!=` for
`MenuItem`.)

**G7 — CLOSED. `tests/` now holds an executable end-to-end suite.**
`tests/e2e_test.cpp` is 2,889 lines and builds as the `aluchop_e2e` CMake target. It stands up a
**real** `services::AppContext` against a throwaway data directory under the system temp dir — so
the migrator seeds a fresh SQLite database, a fresh binary audit trail and a fresh log, and the
user's real database is never opened — then makes **372 hard assertions** against the schema exactly
as the application ships it, printing a PASS/FAIL line for each and exiting non-zero on any failure.
Among other things it asserts that the stored credential is a salted SHA-256 digest and never the
plaintext, that two users with the same password get different salts *and* different hashes, that
the role gate actually refuses a waiter, that the grand total is the sum of the seeded menu prices
exactly and **not** the subtotal grossed up by 13 %, that `Taxable amount + VAT == TOTAL` with no
rounding drift, that the inventory deduction is all-or-nothing, that split totals sum back to the
original subtotal, and that the binary audit trail's checksums and sequence numbers survive a round
trip while a single flipped byte is caught. Several concepts documented above are therefore now
covered by an automated assertion rather than by reading alone.

**Honest caveat:** four of the 372 fail when the machine's local calendar date differs from the UTC
date. The report layer windows its ranges in UTC (`ReportGenerator.cpp:42`, `dayStartUtc`) while the
dashboard's `ReportService::salesForDay()` windows on the *local* day, so for the several hours a
day when the two disagree a report for "today" returns nothing while the dashboard shows takings.
Run where the two dates agree (`TZ=UTC ./build/aluchop_e2e`) the suite is 372/372, exit 0. That is a
real inconsistency in the report layer, not a flaw in any concept above, and it is recorded here
rather than hidden.

**G8 — OPEN (new). Two public counters are implemented, maintained, and never read.**
`Order::openOrderCount()` and `Logger::messagesLogged()` are correct — `Order::s_openCount` is
incremented by all four constructors and decremented by the destructor (§3 #25), and the log counter
is maintained on every write — but `grep -rn "openOrderCount\|messagesLogged" src include tests`
finds no caller outside their own class. The *concept* (static members) is load-bearing several
times over through `Database::instance()`, `Logger::instance()`, `ThemeManager::instance()` and the
static `AuthService::hashPassword`; it is only these two accessors that are unused. A diagnostics
panel is their natural home, which is why this also appears in the README's future-improvements
list.

---

## 11. Two-minute viva tour

Show these five, in this order. They cover the highest-value marks with the least talking.

**1. The diamond — `StaffCustomer` (60 s)**
Open `include/aluchop/models/StaffCustomer.hpp` (the ASCII diagram is at lines 28-35), then
`src/models/StaffCustomer.cpp:37-41`. Say: *"A waiter enrolled in the loyalty programme is one human
with one name. `Employee` and `Customer` both derive `Person` **virtually**, so there is exactly one
`Person` subobject. Because a virtual base is initialised by the most-derived class, `StaffCustomer`
names `Person(...)` itself — the `Person(...)` calls inside `Employee` and `Customer` are skipped.
Remove `virtual` and this file stops compiling: `name()` becomes ambiguous."*
Covers: virtual base, hybrid + multiple inheritance, constructor order, method overriding.

**2. Polymorphic payroll (25 s)**
`src/persistence/EmployeeRepository.cpp:81-92` (the `position` column decides the class →
`make_unique`) then `src/services/EmployeeService.cpp:133-140` (the loop that never asks what it
holds). Say: *"Three columns, all from virtual dispatch. Waiter adds tips, Chef adds overtime at a
static const house rate, Manager adds a bonus — and the payroll loop has no `if` in it."*
Covers: runtime polymorphism, dynamic objects, object pointers, virtual functions, method overriding,
static members.

**3. Split and merge a bill (25 s)**
`src/services/OrderService.cpp:368` (`models::Order breakaway(*original);` — the **copy constructor
is the split**) and `src/services/OrderService.cpp:426` (`*target += *source;` — the **`+=` is the
merge**). Say: *"Because `Order` owns its lines by value, splitting and merging bills are ordinary
C++ value operations, not database gymnastics. The copy resets id and order number, because a copy is
a new unsaved order."*
Covers: copy constructor, operator overloading, objects as members, `operator[]`, RAII.

**4. The binary audit trail (25 s)**
`include/aluchop/persistence/BinaryRecordFile.hpp:39-52` (the 128-byte POD + two `static_assert`s),
then `src/persistence/BinaryRecordFile.cpp:185` (`seekg(index * 128)`), then
`include/aluchop/persistence/AuditTrail.hpp:31` (`: private BinaryRecordFile`). Say: *"Record n is at
byte n×128, so one seek reaches any record. The trail inherits the file **privately** because a trail
is implemented-in-terms-of a record file, not a kind of one — that is what makes `append()`
unreachable and the sequence numbers and checksums impossible to bypass."*
Then **run it**: Reports → *Verify audit trail* → type any index into *"Jump straight to a record"*
and press **Read this record** (`src/gui/ReportsPage.cpp:1362`). One seek, any record, in front of
the marker.
Covers: binary files, random access, structures, arrays, private inheritance, error checking,
destructors/RAII.
*Be ready for the honest caveat:* random-access **reading** is load-bearing and now operable;
random-access **writing** (`overwriteAt`) is deliberately unreachable — see GAPS G3.

**5. Exceptions across the layer boundary (15 s)**
`include/aluchop/core/Exceptions.hpp:33-265` (base + five children), then
`src/persistence/Database.cpp:252-259` (`catch (...) { db.rollback(); throw; }`), then
`src/services/OrderService.cpp:533,540` (two catch clauses, two genuinely different recoveries). Say:
*"Persistence throws, services catch once at their boundary and return a `Result<T>`, so no widget
slot contains a `try`. The bare `throw;` rolls back and lets the original exception through — the
caller sees the real cause, not a substitute."*
Covers: custom exception hierarchy, try/catch/throw, multiple catch, rethrow, class templates
(`Result<T>` + its `void` specialisation).

---

*Verified against the tree as it now stands, after two rounds of edits: every `file:line` above was
re-opened and the ones that had drifted were moved. Re-run `grep -rn "@oop-concept" include src`
after any further edit; prefer the tag over the line number if the two disagree.*

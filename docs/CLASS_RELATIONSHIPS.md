# AluChop — Class Relationships and Why They Are Modelled That Way

> A companion to [`UML_CLASS_DIAGRAM.md`](UML_CLASS_DIAGRAM.md). The diagram shows *what* the
> arrows are; this document argues *why* each one is the right arrow, and what would break if it
> were something else. Every claim is checked against the shipped source, not against the design
> contract — where the two differ, the code wins and the difference is called out.

---

## 1. The people diamond — the centrepiece

### 1.1 The problem, stated in restaurant terms

A restaurant has employees. It has loyalty customers. Sooner or later a waiter joins the loyalty
programme, and now there is one human being who is both. The naive model gives that person two
rows, two names, two phone numbers and two ids, and the moment somebody corrects a typo in one of
them the two halves silently diverge.

### 1.2 The shape in code

```cpp
class Person   { /* abstract: id, name, phone, email, roleName() = 0 */ };
class Employee : virtual public Person { /* position, salary, shift, rating */ };
class Customer : virtual public Person { /* loyaltyPoints, visits, operator++ */ };
class StaffCustomer : public Employee, public Customer { /* the fusion */ };
```

`virtual` on both edges is what makes the base **shared**. `StaffCustomer` carries exactly **one**
`Person` subobject; `setId(7)` is seen by both branches, and `name()` resolves unambiguously.

Delete the two `virtual` keywords and `src/models/StaffCustomer.cpp` stops compiling: `name()`
becomes *"found in multiple base-class subobjects"*, and every read of `m_id` needs an explicit
`Employee::` or `Customer::` qualifier that would then let the two ids drift apart. That is the
whole justification — this is not a diamond invented so the syllabus box could be ticked.

### 1.3 The constructor rule, and why the initialiser list looks odd

A virtual base is initialised by the **most-derived** class only. `StaffCustomer` is that class, so
its member-initialiser list names `Person(...)` directly:

```cpp
StaffCustomer::StaffCustomer(int personId, QString name, QString phone, QString email,
                             QString position, core::Money monthlySalary, QString shift)
    : Person(personId, name, phone, email),
      Employee(personId, name, phone, email, std::move(position), monthlySalary, std::move(shift)),
      Customer(personId, name, phone, email)
{ }
```

The `Person(...)` calls written *inside* `Employee`'s and `Customer`'s own constructors are
**skipped entirely** for this object. Their bodies still run — that is how position, salary, shift
and the enrolment timestamp get set — but only the `Person(...)` line above actually constructs the
identity. If `StaffCustomer` did not name `Person` itself, the identity would be
default-constructed and every field would come out blank. The bases are listed in the order the
language runs them anyway (virtual base, then `Employee`, then `Customer`) so that reading the code
matches what happens.

The identity arguments are **copied, not moved**, into all three initialisers: `Employee` and
`Customer` still need well-formed values for their own bodies, and `QString` is implicitly shared,
so each copy costs one reference-count bump.

### 1.4 `roleName()` is a language requirement, not decoration

`Employee::roleName()` and `Customer::roleName()` are both final overriders of
`Person::roleName()` along *different* paths, which makes the inherited overrider **ambiguous**.
Declaring `StaffCustomer::roleName()` is what makes the class instantiable at all. It returns
`"Staff Member"`.

`displayLabel()` then reaches into both branches in one expression —
`QStringLiteral("%1 (Staff · %2 pts)").arg(name()).arg(loyaltyPoints())` — where `name()` comes
from the shared `Person` and `loyaltyPoints()` from the `Customer` branch. That this is even
well-formed *is* the point of the virtual base.

### 1.5 Where the fusion is actually used

`EmployeeService::staffCustomerFor(int customerId)` builds one. It loads the `customers` row, loads
the roster, and matches on **normalised phone number** against active employees. If it finds a
match it constructs the `StaffCustomer` and copies the employment attributes from the staff row and
the loyalty attributes from the guest row onto that one identity.

`BillingService::prepareBill()` then asks that single object two questions that live on opposite
branches — *"what is your staff discount?"* (`staffDiscountPercent()`, 10 %) and, via the customer
record, *"what is your loyalty balance?"* — without carrying two identities. When the lookup
cannot be completed the method returns `std::nullopt` and billing simply offers no staff discount;
a failed fusion must never stop the till taking money.

### 1.6 The rest of the people hierarchy

| Edge | Form | Why |
|---|---|---|
| `Waiter : public Employee` | hierarchical | overrides `monthlyPay()` = salary + tips |
| `Chef : public Employee` | hierarchical | overrides `monthlyPay()` = salary + overtime × `kOvertimeRatePerHour` |
| `Manager : public Employee` | single | overrides `monthlyPay()` = salary + bonus; `m_bonus` is `protected` so `Admin` can reach it |
| `Admin : public Manager, public IAuditable` | multilevel **and** multiple | `Person → Employee → Manager → Admin` is four real levels; `IAuditable` is a capability, not a role |

`Admin::roleName()` is declared **`final`**. That is a deliberate statement: no role may ever
masquerade as something below `Admin` by re-overriding it further down.

Payroll is the honest polymorphism test. `EmployeeRepository::allTyped()` returns
`std::vector<std::unique_ptr<models::Employee>>` built from the `position` column, and
`EmployeeService::payrollPreview()` walks that vector calling `monthlyPay()`. Four different
formulas run through one virtual call, on objects whose concrete type was decided by a database
string.

---

## 2. Interfaces as pure-abstract mixins

Four interfaces, all state-free, all with a virtual destructor and nothing else:

| Interface | Implemented by | Why it is a mixin and not a base class |
|---|---|---|
| `IPrintable` | `Order`, `Bill` | "can render itself as monospace text" is orthogonal to what the thing *is*. A kitchen ticket and a customer receipt share no data, only a capability. |
| `ISerializable` | `MenuItem` | Only the menu round-trips through JSON — `SchemaMigrator` seeds `MenuItem` straight from `assets/menu/menu_seed.json` via `fromJson()`. Putting `toJson()` on a common base would force it on twenty classes that never need it. |
| `IAuditable` | `Admin` | Gives `Admin` a one-line audit description without dragging auditing into `Person`. |
| `IDiscountable` | `Bill` | The discount contract is `setDiscount(amount, label)` + `discount()`. `Bill` is the only thing a discount can be applied to, but stating it as an interface is what lets `BillingService` reason about "a discountable" rather than about `Bill`'s internals. |

`Bill : public IPrintable, public IDiscountable` is the second genuine multiple-inheritance site: a
bill really is *both* a printable document *and* a discount target, and neither capability implies
the other.

---

## 3. `Bill` and `BillingService` — the friend relationship

`Bill::settle(PaymentMethod, Money tendered, Money change)` is **private**, and the class declares
exactly one friend:

```cpp
class Bill : public IPrintable, public IDiscountable {
    friend class aluchop::services::BillingService;
    // ...
private:
    void settle(PaymentMethod m, core::Money tendered, core::Money change);
```

The consequence is architectural, not cosmetic. There is exactly **one line in the entire
application** where a bill can transition to "paid" — inside `BillingService::settle()` — and it
executes *after* the database transaction that inserted the `payments` row has committed. No GUI
code, no test, no future service can mark a bill paid without money having been recorded first.
That is what a `friend` is for: a deliberate, named, minimal hole in encapsulation, not a
convenience.

A second friendship, `std::ostream& operator<<(std::ostream&, const Bill&)`, is a **friend
function** rather than a member because the left operand is the stream — a member `operator<<`
would put the bill on the left, which is backwards. It reads the bill's private members directly
rather than touring public accessors, which is exactly the case `friend` exists for.
`Money::operator<<` is the same pattern for the same reason. *Both are implemented and correct but
have no call site in the shipped build:* the receipt path goes through
`IPrintable::toPrintableText()` instead, so `operator<<` is a demonstrated capability rather than a
load-bearing one. See `OOP_COVERAGE.md` G1.

### `Bill` composes, it does not reference

`Bill` holds `std::vector<OrderItem> m_items` **by value** and copies the subtotal at construction:

```cpp
explicit Bill(const Order& order);   // snapshots items, subtotal, ids
```

It stores `m_orderId` / `m_orderNumber`, not an `Order*`. From the moment the bill exists, the
order can be edited, the menu can be re-priced and a dish can be deleted without changing what the
guest owes. A pointer or reference here would be a lifetime bug and a correctness bug at once.
`Bill.hpp` forward-declares `Order` for exactly this reason — it is *built from* an order but does
not contain one.

---

## 4. `Order` — composition, value semantics, and two operators that mean something

### 4.1 Composition of lines

`Order` owns `std::vector<OrderItem> m_items` **by value** — true composition. Destroy the order
and the lines go with it, which mirrors `order_items.order_id ... ON DELETE CASCADE` in the
schema. `OrderItem` has no back-pointer to its `Order`: a line is a value, not an entity, and
nothing about it is meaningful outside its order.

The line itself is a **snapshot**: `m_name` and `m_unitPrice` are frozen at order time and never
re-read from `menu_items`. That is why `order_items.menu_item_id` is nullable with
`ON DELETE SET NULL` — deleting a dish must never rewrite a printed bill.

### 4.2 The copy constructor *is* the split-bill feature

```cpp
Order::Order(const Order& other)
    : m_id(0)          // a copy is a NEW, unsaved order …
    , m_orderNumber()  // … so it may not inherit a unique order number
    , /* type, status, table, customer, waiter, createdAt, note copied */
    , m_items(other.m_items)   // deep copy: the vector holds OrderItem VALUES
{ ++s_openCount; }
```

Resetting `m_id` and `m_orderNumber` is what makes the copy *semantically* a new order rather than
a duplicate of an existing one — `orders.order_number` is `UNIQUE`, so inheriting it would be a
constraint violation waiting to happen. `OrderService::splitOrder()` then relies on exactly this:
it copies the order, strips the lines that stay behind from the copy and the lines that leave from
the original, and writes both in one transaction. A persisted breakaway whose parent still held the
same lines would bill the guest twice.

`operator=` has the same deep-copy semantics. Move constructor and move assignment also exist
(they keep the live-object counter correct) — *this is an addition beyond the frozen contract in
`ARCHITECTURE.md` §3.2, which specifies only copy operations.*

### 4.3 `operator+=` *is* the merge feature

```cpp
Order& Order::operator+=(const Order& other) {
    if (this == &other) return *this;       // self-merge would double every line
    for (const OrderItem& line : other.m_items) addItem(line);
    return *this;
}
```

`addItem` already merges quantities when the `menuItemId` matches, so absorbing another order is a
three-line loop. Only the lines move: the target keeps its own id, number, table and status.
`OrderService::mergeOrders()` then cancels the source rather than deleting it, and stamps
`orders.merged_into` — the audit trail must always be able to explain where those lines came from.
Compound addition is the domain meaning of "put it all on one bill", which is why this operator is
natural rather than clever.

### 4.4 `operator[]` and the status ladder

`operator[]` exists in const and non-const forms, both throwing `std::out_of_range`, because an
order genuinely *is* a sequence of lines. The non-const overload returns `OrderItem&` — return by
reference — so a caller edits the line in place.

`setStatus()` consults one table, `isLegalTransition()`, and throws `core::ValidationException`
otherwise. Keeping that table in the **model** rather than the GUI that draws the buttons is what
makes it impossible for any code path to mark an order `PAID` straight from `OPEN`.
`from == to` is deliberately legal so that a redundant write is a no-op, not a crash.

### 4.5 The static counter

`Order::s_openCount` is a private static incremented by every constructor (including copy and move)
and decremented by the destructor; `openOrderCount()` reads it. It is a genuine static data member
with a static member function accessor. **Honest caveat:** nothing in the shipped build calls
`openOrderCount()` — the dashboard's "pending orders" tile uses
`ReportService::pendingOrderCount()`, which is a database query and is the right source for a
*persistent* count. The counter measures live C++ objects, which is a different thing.
`ARCHITECTURE.md` §3.2 says "the dashboard uses it"; it does not.

---

## 5. `Repository<T>` — the class template at the persistence boundary

```cpp
template <typename T>
class Repository {
public:
    explicit Repository(QString tableName);
    std::vector<T> findAll() const;
    std::optional<T> findById(int id) const;
    int count() const;
    void removeById(int id);
protected:
    virtual T fromRecord(const QSqlRecord& rec) const = 0;   // pure virtual
    virtual QString orderByClause() const;                   // virtual, defaults to "id"
    const QString m_table;
};
```

This is the one place in the codebase where **compile-time and runtime polymorphism cooperate**.
The template supplies the four operations that are identical for every table; the pure virtual
`fromRecord` supplies the one operation that is different for every table. Eleven repositories
derive from it — `User`, `Menu`, `Customer`, `Employee`, `Order`, `Ingredient`, `Supplier`,
`Table`, `Reservation`, `Payment`, `Promo` — and each writes only its own hydration and its own
sort order.

`m_table` is `const`: a repository's table is fixed at construction and no derived class can point
it somewhere else. That, plus the fact that the table name never comes from user input, is what
makes the `%1` substitution in `SELECT * FROM %1` safe; every *value* is bound through
`Database::prepared()` with a `QVariantList`, never concatenated.

**Two repositories deliberately do not derive it.** `SettingsRepository` has no `id` column at all
(`settings` is keyed by `key`), and `AuditRepository` is an append-only log rather than an entity
store. Inheriting `findById`/`removeById` would give both classes an API that cannot mean anything.
Not forcing a base class onto a type that does not fit it is a design decision, not an omission.

---

## 6. Private inheritance — `AuditTrail : private BinaryRecordFile`

```cpp
class AuditTrail : private BinaryRecordFile {
public:
    explicit AuditTrail(const QString& path);
    std::uint32_t record(std::uint32_t userId, const QString& action, const QString& entity,
                         core::Money amount, const QString& details);
    AuditRecord at(std::size_t index);
    std::vector<AuditRecord> tail(std::size_t n);
    std::size_t size();
    bool verifyIntegrity(std::size_t& firstBadIndex);
    using BinaryRecordFile::close;      // selectively re-exposed
private:
    std::uint32_t m_nextSeq = 1;
};
```

An audit trail **is not** a binary record file — it is *implemented in terms of* one. That
distinction is exactly what `private` inheritance expresses, and it has teeth here:
`BinaryRecordFile::append()` and `overwriteAt()` write a raw 128-byte record with no sequence
number and no checksum. If they were public on `AuditTrail`, a caller could append a record that
breaks the strictly-increasing `seq` invariant or carries a wrong checksum, and
`verifyIntegrity()` would then report corruption that the class itself had allowed.

Making the base private closes that door while still reusing every byte of the implementation.
`using BinaryRecordFile::close;` re-exposes the one operation that is safe to hand out — closing
the file cannot violate an invariant.

Composition would also have worked. Private inheritance was chosen because `AuditTrail` needs the
protected `ensureOpen()` and the protected `m_stream`/`m_path` members, which a member object
would not give it.

---

## 7. Protected inheritance — `ReportGenerator : protected persistence::CsvWriter`

`protected` sits between the other two for a reason that is visible in the design:

* **Not public** — a report is not a CSV writer. Nobody outside should be able to call
  `report.writeRow(...)` and interleave arbitrary rows into a half-written export.
* **Not private** — the five concrete reports (`SalesReport`, `InventoryReport`, `OrdersReport`,
  `CustomersReport`, `EmployeesReport`) are *derived* classes, and they must be able to drive the
  writer. Private inheritance would hide it from them too.

`ReportGenerator::exportCsv(outPath)` is the only public door: it opens the writer, writes
`header()`, writes every row from `rows()`, closes, and — if anything throws — closes and
**re-throws untouched with a bare `throw;`** so a half-written file is never reported as success.

The three pure virtuals (`title()`, `header()`, `rows()`) are the template method's variation
points; `ReportService::makeReport(kind, from, to)` is the factory that returns
`std::unique_ptr<ReportGenerator>` and is the only reason the GUI can export five different reports
through one code path.

---

## 8. The service layer boundary

### 8.1 The rule

```
gui  →  services  →  persistence  →  models  →  core
```

Strictly one-way. Two greppable invariants make it mechanical rather than aspirational, and both
return nothing on the shipped tree:

```bash
# No SQL may appear in the services layer.
grep -rnE 'QSqlQuery|QSqlDatabase|QSqlRecord|SELECT |INSERT |UPDATE |DELETE ' \
     src/services include/aluchop/services

# The GUI may not name a persistence type — that is what "the GUI never touches SQL" means.
grep -rnE 'persistence::|aluchop/persistence/' src/gui include/aluchop/gui src/main.cpp
```

The second rule is why `AuditService` grew `verifyTrailIntegrity()`, `trailRecordCount()` and
`recentTrailRecords()`: the Reports page needs to show audit-trail health, and it may not reach
into `persistence::AuditTrail` to get it. The service re-exports what the GUI legitimately needs.

### 8.2 What a service returns

Services never let a persistence exception escape to the GUI. They catch `core::AluChopException`
and its subtypes at the boundary and convert them into `core::Result<T>` — a value the GUI can
display. The exceptions still exist and still carry their category and context; they simply stop at
the layer that knows how to phrase them for a human.

`OrderService::advanceStatus()` is the clearest example of *multiple catch* being meaningful rather
than decorative:

* `InventoryException` → the store room disagrees with the recipe book, but the plate is already on
  the guest's table. Correct recovery: a loud warning and a stock discrepancy to fix later.
  The call returns **success** — refusing to serve food that has already been cooked would be
  worse than a wrong stock figure.
* `DatabaseException` → the status change itself did not persist. The user must be told it failed.
* Anything else in the hierarchy → a generic error.

Three catch clauses, three genuinely different recoveries.

### 8.3 `AppContext` — the composition root

`AppContext` owns **every** repository and **every** service as a **value member**, in declaration
order, and hands out non-owning references through accessors. Three consequences:

1. **Ownership is trivially correct.** One object's lifetime governs everything. `main.cpp` keeps
   it in a `std::unique_ptr` living in the frame that calls `app.exec()`, so the composition root
   provably outlives every window holding a reference to it.
2. **Wiring is explicit and checked by the compiler.** Services take their collaborators as
   `Type&` constructor parameters, so a missing dependency is a build error, not a null pointer at
   runtime. There is no service locator and no singleton lookup inside a service.
3. **Declaration order is load-bearing.** The first member is a private nested
   `struct DbBootstrap`, whose constructor creates the data directory, points the logger at it,
   opens SQLite and runs the migrations. Because C++ initialises members in declaration order, this
   *must* stay first: every repository declared after it can assume an open, migrated database.
   Moving it would produce repositories constructed against a database that does not exist yet.

Services are declared after repositories, and inter-dependent services after their dependencies
(`NotificationService` and `AuditService` first, then `AuthService`, then the domain services, with
`OrderService` after `InventoryService` and `CustomerService` because it holds references to both).

### 8.4 References, not pointers

Every service member is `Type&`, never `Type*`:

```cpp
persistence::OrderRepository& m_orders;
CustomerService&              m_customers;
AuditService&                 m_audit;
NotificationService&          m_notify;
```

A reference cannot be null and cannot be re-seated. Combined with `AppContext` owning the referents
by value, this makes "is this pointer still valid?" a question that cannot arise. The classic Qt
alternative — raw pointers with `QObject` parenting — would work for widgets but buys nothing for
plain business objects and costs a null check at every use.

---

## 9. Ownership rules, stated once

| Kind of object | Owner | How it dies |
|---|---|---|
| Repositories and services | `AppContext`, by value | `AppContext` destructor, reverse declaration order |
| `AppContext` | `std::unique_ptr` in `runSession()`'s frame | after `app.exec()` returns |
| Widgets | Their Qt parent | Qt deletes the tree; there is not one manual `delete` in the GUI layer |
| Top-level windows | Nobody — created with `new`, no parent | `Qt::WA_DeleteOnClose` |
| `std::unique_ptr<Employee>` from `makeTyped`/`allTyped` | The caller | scope exit |
| `std::unique_ptr<ReportGenerator>` from `makeReport` | The caller | scope exit |
| `std::unique_ptr<Command>` handed to `CommandStack::run` | The stack (capped at 50) | popped past the cap, or cleared by a new action |
| `std::fstream` / `std::ofstream` in `BinaryRecordFile`, `CsvWriter`, `Logger` | The owning object | destructor flushes and closes — RAII, and the destructors swallow errors because a destructor must not throw |
| `QSqlDatabase` connection `aluchop_main` | `Database` singleton | `Database` destructor |

The single rule that makes all of this work: **an object that holds a reference must be destroyed
before the object it refers to.** `AppContext`'s member order gives that for free inside the
context, and `main.cpp`'s frame layout gives it for free between the context and the windows.

---

## 10. Association, aggregation, composition — where each is used and why

| Relationship | Example | Kind | Reasoning |
|---|---|---|---|
| `Order` → `OrderItem` | `std::vector<OrderItem> m_items` | **Composition** | Lines have no meaning outside their order; the schema cascades the delete. |
| `Bill` → `OrderItem` | `std::vector<OrderItem> m_items` | **Composition** (of *copies*) | A bill is a frozen snapshot; sharing the order's lines would let the bill change after it was quoted. |
| `AppContext` → services | value members | **Composition** | One lifetime governs the whole graph. |
| `OrderService` → `KitchenQueue` | value member | **Composition** | The queue is an implementation detail of order flow; `kitchenQueue()` hands out a reference for reading, not ownership. |
| `MainWindow` → `Sidebar`, `ToastHost`, `CommandPalette` | Qt parent-child | **Composition** | Qt destroys children with the parent. |
| `MainWindow` → the nine `Page`s | `std::array<Page*, 9>`, held in a `QStackedWidget` | **Aggregation** | The stack widget is the real owner; `MainWindow` keeps typed handles so it can call `refresh()`. |
| `Page` → `AppContext` | `AppContext& m_ctx` | **Association** | The page uses the context; it emphatically does not own it. |
| `Order` → `Table`, `Customer`, `Employee` | plain `int` ids | **Association by id** | Deliberate: models perform no I/O, so an order cannot hold a live `Table` object without dragging persistence into `aluchop::models`. `0` means "none", which mirrors the nullable foreign keys exactly. |
| `Ingredient` → `Supplier` | `int m_supplierId` | **Association by id** | Same reason; `ON DELETE SET NULL` in the schema, `0` in the model. |
| `RecipeLine` | two ids + a quantity | **Association class** | The junction table `recipes` made explicit as a `struct`. It is a POD because it has no invariant of its own. |
| `Bill` ← `BillingService` | `friend` | **Privileged association** | See §3. |
| `InventoryService` → `MenuRepository` | reference | **Association** | It reads recipes; it does not own the menu. |

The "association by id" choice deserves emphasis because it is the one that keeps the layer rule
honest. If `Order` held a `Table` object, hydrating an order would require reading the tables
table, which would mean `aluchop::models` performing I/O, which would collapse the whole
dependency direction. Ids keep the model layer a pure, testable, I/O-free description of the
domain.

---

## 11. Interface segregation in the GUI — `Page`

```cpp
class Page : public QWidget {
    Q_OBJECT
public:
    explicit Page(services::AppContext& ctx, QWidget* parent = nullptr);
    virtual QString pageTitle() const = 0;
    virtual void refresh() = 0;
protected:
    services::AppContext& m_ctx;
};
```

Two pure virtuals and one protected reference are the entire contract. That is enough for
`MainWindow` to do everything it needs: title the header, refresh the visible page on `F5`, and
re-refresh whichever page is showing whenever `NotificationService::dataChanged(domain)` fires.
Nine concrete pages implement it, all marked `final`, and `MainWindow` never downcasts except in
one deliberate place — `qobject_cast<OrdersPage*>` for the `Ctrl+N` "new order" shortcut, which
needs a page-specific public slot.

`refresh()` carries an implicit contract the pages honour: it must be **idempotent and must never
throw**, because it is called from signal handlers where an escaping exception would cross a Qt
event-loop boundary.

---

## 12. The observer relationship — `NotificationService`

`NotificationService` is the **only** service that derives `QObject` and carries `Q_OBJECT`. It
emits two signals:

```cpp
void notification(const QString& title, const QString& message, int level);
void dataChanged(const QString& domain);
```

Every other service takes a `NotificationService&` and calls `notify(...)` or
`announceDataChanged("orders" | "inventory" | "customers" | "payments" | "reservations")`.
`MainWindow` connects both signals once, raises a `Toast` for the first and refreshes the visible
page for the second.

This is what keeps the dependency arrow one-way. `InventoryService` needs the Inventory page to
redraw after a stock deduction — but a service may not know that a GUI exists. It announces a fact
("inventory changed"); whoever cares subscribes. Adding a tenth page requires no change to any
service.

`level` is passed as a plain `int` rather than `models::NoticeLevel` so that the signal signature
does not drag a models header into every connection site; the mapping is Info 0 / Success 1 /
Warning 2 / Danger 3.

---

## 13. The Command pattern — undo/redo

```cpp
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual QString description() const = 0;
};
```

Four concrete commands, all `final`: `AddOrderItemCommand`, `RemoveOrderItemCommand`,
`ToggleAvailabilityCommand`, `AdjustStockCommand`. Each holds a **reference** to the service it
drives plus the small amount of state its inverse needs — `AddOrderItemCommand` records where the
line landed, `RemoveOrderItemCommand` captures the menu item and quantity at `execute()` time so
`undo()` can restore the line.

`CommandStack` owns them as `std::vector<std::unique_ptr<Command>>` (undo and redo stacks), capped
at `kMaxDepth = 50`. `run()` executes and pushes; a fresh action clears the redo stack, which is
the standard and correct semantics.

The relationship worth naming is *why the commands hold services rather than models*: undo has to
be **durable**. Reversing an in-memory `Order` object would leave the database holding the change.
Going back through the service means the inverse takes the same validated, audited, transactional
path the original did.

---

## 14. Singletons — three of them, and why each is defensible

| Singleton | Justification |
|---|---|
| `core::Logger::instance()` | A Meyers singleton over one append-mode `std::ofstream`. There is exactly one log file, and threading it through forty classes as a parameter would be noise. Copy and assignment are `= delete`. Carries `static int s_messageCount`. |
| `persistence::Database::instance()` | One `QSqlDatabase` connection named `aluchop_main`. `QSqlDatabase` connections are per-thread and must never be copied across threads; the application is single-threaded by contract, so one process-wide handle is both correct and simplest. |
| `gui::ThemeManager::instance()` | The palette is a property of the running application, and `themeChanged()` must reach every widget. A second theme manager would mean two truths about what colour a card is. |

`AppContext` is deliberately **not** a singleton — it is constructed once in `main.cpp` and passed
by reference. That is what makes the dependency graph explicit and inspectable.

---

## 15. Drift from `ARCHITECTURE.md`

The frozen contract calls its file manifest *binding*. The shipped tree matches it for every file
it names, and adds the following. All of it is additive; none of it contradicts a rule.

| Item | Status |
|---|---|
| `include/aluchop/gui/Widgets.hpp` (`GlassPanel`, `ElegantTable`, `SearchBar`, `ChartCard`, `EmptyState`) | Header-only shared widget vocabulary. **Not in the manifest.** |
| `gui::LoadingOverlay` (inside `SplashScreen.hpp`) | Busy overlay. Not in the contract's `SplashScreen` description — **and currently never instantiated** by any translation unit. |
| `core::AluChopException` with `(what, context, code)`, `message()`, `context()`, `code()`, `category()` | Contract specifies only `explicit AluChopException(const std::string&)`. Every subclass gained the richer constructor plus a `category()` override; `ValidationException::field()` and `FileIOException::path()` are extra. |
| `Order` move constructor and move assignment | Contract specifies copy operations only. |
| `Result<T>::isErr()`, `Result<void>::isErr()` | Additions. |
| `Logger::levelName()`, `logFilePath()`, `isOpen()` | Additions. |
| `MenuItem::operator!=` | Contract specifies `operator==` and `operator<` only. |
| `models::toString(NoticeLevel)` / `noticeLevelFromString` | Contract lists conversions for six enums, not seven. |
| `core::kAttributionBlock`, `kCopyrightNotice`, `namespace sage`, `namespace tuning` in `AppInfo.hpp` | Contract specifies `kAppInfo` and `kMenuCategories` only. |
| `Money.hpp` declares `operator<<` at namespace scope as well as as a friend | Correct C++ (a friend declaration alone does not make the name findable by ordinary lookup); not in the contract's listing. |
| `Order::openOrderCount()` and `Logger::messagesLogged()` are never called | Contract says the dashboard uses the former. It does not — `ReportService::pendingOrderCount()` is used instead. Both accessors are defined and correct, but currently unreferenced. |
| Role enforcement | Contract implies role-based authorisation across the app. Shipped: enforced in `AuthService::createUser` (Admin) and `EmployeesPage` (Manager+) only. See `USE_CASE.md` §5. |

---

<sub>© 2026 AluChop Restaurant Management System. Developed by Shashank Bhattarai (ACE082BCT078).
For academic use as an ENCT151 Object-Oriented Programming coursework project. All rights reserved.</sub>

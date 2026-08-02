# AluChop — UML Class Diagrams

> Every class, member and arrow below was read out of the real headers in
> `include/aluchop/**` and the real definitions in `src/**`. Where the code and
> [`ARCHITECTURE.md`](ARCHITECTURE.md) disagree, **the code is what is drawn here.**
>
> The graph is ~70 classes across five namespaces, so it is split by layer. Diagram 1 is the
> centrepiece — the `Person` / `Employee` / `Customer` / `StaffCustomer` virtual-base diamond.
>
> Notation: `<|--` inheritance · `<|..` interface realisation · `*--` composition (owner destroys
> the part) · `o--` aggregation (holds, does not own) · `-->` association · `..>` dependency.
> Generic parameters are written `Type~T~` because that is Mermaid's syntax for `Type<T>`.

---

## 1. The people diamond — `aluchop::models`

This is the required *virtual base class* and it solves a real problem, not a contrived one.
A waiter who is also enrolled in the loyalty programme is **one human being** with one name, one
phone number and one id, who happens to appear in two database tables. `StaffCustomer` is that
person. Because `Employee` and `Customer` both derive `Person` **virtually**, the object carries
exactly one `Person` subobject: `setId(7)` is seen by both branches, and `name()` compiles.
Remove `virtual` from either edge and `StaffCustomer.cpp` stops compiling — `name()` becomes
*"found in multiple base-class subobjects"*.

```mermaid
classDiagram
    direction TB

    class Person {
        <<abstract>>
        #int m_id
        #QString m_name
        #QString m_phone
        #QString m_email
        +Person(int id, QString name, QString phone, QString email)
        +roleName()* QString
        +displayLabel() QString
        +id() int
        +setId(int id) void
        +name() QString
        +phone() QString
        +email() QString
        +setName(QString v) void
        +setPhone(QString v) void
        +setEmail(QString v) void
    }

    class Employee {
        #QString m_position
        #Money m_salary
        #QString m_shift
        #QDate m_hired
        #bool m_active
        #int m_rating
        +Employee(int id, QString name, QString phone, QString email, QString position, Money salary, QString shift)
        +roleName() QString
        +monthlyPay() Money
        +position() QString
        +salary() Money
        +setSalary(Money v) void
        +shift() QString
        +hiredDate() QDate
        +isActive() bool
        +performanceRating() int
        +setPerformanceRating(int r) void
    }

    class Customer {
        -int m_loyaltyPoints
        -int m_visits
        -QDateTime m_created
        +Customer(int id, QString name, QString phone, QString email)
        +roleName() QString
        +loyaltyPoints() int
        +addLoyaltyPoints(int pts) void
        +redeemPoints(int pts) void
        +visits() int
        +createdAt() QDateTime
        +operator++() Customer
        +operator++(int) Customer
    }

    class Waiter {
        -Money m_tips
        -int m_tablesServed
        +roleName() QString
        +monthlyPay() Money
        +tipsThisMonth() Money
        +addTip(Money t) void
        +tablesServed() int
    }

    class Chef {
        -QString m_specialty
        -int m_overtimeHours
        +kOvertimeRatePerHour Money$
        +roleName() QString
        +monthlyPay() Money
        +specialty() QString
        +overtimeHours() int
        +setOvertimeHours(int h) void
    }

    class Manager {
        #Money m_bonus
        +roleName() QString
        +monthlyPay() Money
        +monthlyBonus() Money
        +setMonthlyBonus(Money b) void
    }

    class Admin {
        +roleName() QString
        +auditDescription() QString
        +canManageUsers() bool
    }

    class StaffCustomer {
        +StaffCustomer(int personId, QString name, QString phone, QString email, QString position, Money salary, QString shift)
        +roleName() QString
        +displayLabel() QString
        +staffDiscountPercent() int
    }

    class IAuditable {
        <<interface>>
        +auditDescription()* QString
    }

    Person <|-- Employee : virtual public
    Person <|-- Customer : virtual public
    Employee <|-- Waiter : public
    Employee <|-- Chef : public
    Employee <|-- Manager : public
    Manager <|-- Admin : public
    IAuditable <|.. Admin
    Employee <|-- StaffCustomer : public
    Customer <|-- StaffCustomer : public
```

**Which inheritance form lives where**

| Form | Site | Why it is genuine |
|---|---|---|
| Single | `Manager : public Employee` | one base, adds a bonus and its own pay policy |
| Hierarchical | `Waiter`, `Chef`, `Manager : public Employee` | three real siblings over one base |
| Multilevel | `Person → Employee → Manager → Admin` | every level adds state and behaviour |
| Multiple | `Admin : public Manager, public IAuditable` | a concrete role plus a capability mixin |
| Hybrid | `StaffCustomer : public Employee, public Customer` | multiple inheritance over a virtual/hierarchical structure |
| **Virtual base** | `Employee : virtual public Person`, `Customer : virtual public Person` | one identity for a staff member who is also a guest |
| Method overriding | `monthlyPay()` in Waiter/Chef/Manager; `roleName()` everywhere (`final` in `Admin`) | payroll and labels are honestly polymorphic |

`StaffCustomer::roleName()` is not decoration: `Employee::roleName()` and `Customer::roleName()`
are both final overriders of `Person::roleName()` along different paths, so the inherited overrider
is ambiguous. Declaring one in `StaffCustomer` is a **language requirement** for the class to be
instantiable at all.

---

## 2. `aluchop::core` — value types, errors, logging

Depends on QtCore and the standard library only. Nothing in this layer performs I/O except
`Logger`, which owns a `std::ofstream` in append mode.

```mermaid
classDiagram
    direction LR

    class Money {
        -int64 m_paisa
        +Money(int64 paisa)
        +fromRupees(int64 rupees, int64 paisa) Money$
        +zero() Money$
        +paisa() int64
        +wholeRupees() int64
        +isZero() bool
        +isNegative() bool
        +toString() QString
        +percent(int pct) Money
        +operator+=(Money rhs) Money
        +operator-=(Money rhs) Money
        +operator*=(int64 factor) Money
        +operator<<(ostream os, Money m) ostream$
    }

    class Result~T~ {
        -optional~T~ m_value
        -QString m_error
        +ok(T value) Result~T~$
        +err(QString message) Result~T~$
        +isOk() bool
        +isErr() bool
        +value() T
        +take() T
        +error() QString
        +valueOr(U fallback) T
    }

    class AluChopException {
        -string m_message
        -string m_full
        -string m_context
        -int m_code
        +AluChopException(string what)
        +AluChopException(string what, string context, int code)
        +what() char
        +message() string
        +context() string
        +code() int
        +category() char
    }
    class DatabaseException {
        +category() char
    }
    class ValidationException {
        +category() char
        +field() string
    }
    class AuthException {
        +category() char
    }
    class InventoryException {
        +category() char
    }
    class FileIOException {
        +category() char
        +path() string
    }

    class Logger {
        -ofstream m_out
        -QString m_path
        -int s_messageCount$
        +instance() Logger$
        +setLogFile(QString path) void
        +log(QString message) void
        +log(Level level, QString message) void
        +debug(QString m) void
        +info(QString m) void
        +warn(QString m) void
        +error(QString m) void
        +messagesLogged() int$
        +levelName(Level level) char$
        +isOpen() bool
    }

    class AppInfo {
        <<struct>>
        +char appName
        +char version
        +char developer
        +char rollNo
        +char email
    }

    class Algorithms {
        <<utility>>
        +sumMoney(Container c, Projection proj) Money$
        +countMatching(Container c, Predicate pred) int$
        +clampValue(T v, T lo, T hi) T$
    }

    RuntimeError <|-- AluChopException
    AluChopException <|-- DatabaseException
    AluChopException <|-- ValidationException
    AluChopException <|-- AuthException
    AluChopException <|-- InventoryException
    AluChopException <|-- FileIOException
    Algorithms ..> Money : sums
    Logger ..> FileIOException : throws
```

`RuntimeError` in the diagram is `std::runtime_error`. `Money` is the only currency type in the
program: **no `double` ever holds money**, at any layer, not even in transit.

---

## 3. `aluchop::models` — the transactional domain

```mermaid
classDiagram
    direction TB

    class IPrintable {
        <<interface>>
        +toPrintableText()* QString
    }
    class ISerializable {
        <<interface>>
        +toJson()* QJsonObject
        +fromJson(QJsonObject obj)* void
    }
    class IDiscountable {
        <<interface>>
        +setDiscount(Money amount, QString label)* void
        +discount()* Money
    }

    class MenuItem {
        -int m_id
        -QString m_name
        -QString m_category
        -QString m_description
        -QString m_imagePath
        -Money m_price
        -bool m_available
        +MenuItem(int id, QString name, QString category, Money price, QString description)
        +category() QString
        +setCategory(QString v) void
        +price() Money
        +isAvailable() bool
        +setAvailable(bool a) void
        +toJson() QJsonObject
        +fromJson(QJsonObject obj) void
        +operator==(MenuItem a, MenuItem b) bool$
        +operator!=(MenuItem a, MenuItem b) bool$
        +operator<(MenuItem rhs) bool
    }

    class OrderItem {
        -int m_menuItemId
        -QString m_name
        -Money m_unitPrice
        -int m_qty
        -QString m_note
        +OrderItem(int menuItemId, QString nameSnapshot, Money unitPrice, int qty)
        +menuItemId() int
        +name() QString
        +unitPrice() Money
        +qty() int
        +setQty(int q) void
        +lineTotal() Money
    }

    class Order {
        -int m_id
        -QString m_orderNumber
        -OrderType m_type
        -OrderStatus m_status
        -int m_tableId
        -int m_customerId
        -int m_waiterId
        -QDateTime m_createdAt
        -QString m_note
        -vector~OrderItem~ m_items
        -int s_openCount$
        +Order()
        +Order(Order other)
        +operator=(Order other) Order
        +addItem(OrderItem item) void
        +addItem(int menuItemId, QString name, Money unitPrice, int qty) void
        +removeItemAt(size_t index) void
        +itemCount() size_t
        +items() vector~OrderItem~
        +operator[](size_t i) OrderItem
        +operator+=(Order other) Order
        +subtotal() Money
        +status() OrderStatus
        +setStatus(OrderStatus s) void
        +toPrintableText() QString
        +openOrderCount() int$
    }

    class Bill {
        -int m_orderId
        -QString m_orderNumber
        -vector~OrderItem~ m_items
        -Money m_subtotal
        -Money m_discount
        -Money m_serviceCharge
        -Money m_tendered
        -Money m_change
        -QString m_discountLabel
        -QString m_promoCode
        -PaymentMethod m_method
        -bool m_settled
        +Bill(Order order)
        +subtotal() Money
        +discount() Money
        +setDiscount(Money amount, QString label) void
        +serviceCharge() Money
        +setServiceCharge(Money v) void
        +total() Money
        +isSettled() bool
        +toPrintableText() QString
        -settle(PaymentMethod m, Money tendered, Money change) void
    }

    class Payment {
        -int m_id
        -int m_orderId
        -int m_promoId
        -int m_receivedBy
        -PaymentMethod m_method
        -Money m_subtotal
        -Money m_discount
        -Money m_serviceCharge
        -Money m_total
        -Money m_tendered
        -Money m_change
        -QDateTime m_paidAt
        +total() Money
        +tendered() Money
        +change() Money
        +paidAt() QDateTime
    }

    class Promo {
        -int m_id
        -int m_percent
        -QString m_code
        -PromoKind m_kind
        -Money m_flat
        -Money m_minOrder
        -QDate m_validFrom
        -QDate m_validTo
        -bool m_active
        +isValidOn(QDate day, Money orderSubtotal) bool
        +discountFor(Money subtotal) Money
    }

    class Table {
        -int m_id
        -int m_capacity
        -QString m_name
        -bool m_active
        +capacity() int
        +isActive() bool
    }

    class Reservation {
        -int m_id
        -int m_tableId
        -int m_customerId
        -int m_durationMin
        -int m_guests
        -QString m_customerName
        -QString m_phone
        -QString m_specialRequest
        -QDateTime m_startsAt
        -ReservationStatus m_status
        +endsAt() QDateTime
        +overlaps(QDateTime start, int durationMin) bool
    }

    class Ingredient {
        -int m_id
        -int m_supplierId
        -QString m_name
        -QString m_unit
        -double m_stockQty
        -double m_lowThreshold
        -QDate m_expiry
        -Money m_unitCost
        +isLow() bool
        +expiresWithin(int days) bool
    }

    class Supplier {
        -int m_id
        -QString m_name
        -QString m_phone
        -QString m_email
        -QString m_address
    }

    class RecipeLine {
        <<struct>>
        +int menuItemId
        +int ingredientId
        +double qtyPerServing
    }

    class User {
        -int m_id
        -QString m_username
        -UserRole m_role
        -QString m_passHash
        -QString m_salt
        -int m_employeeId
        -QString m_securityQuestion
        -QString m_securityAnswerHash
        -QString m_rememberToken
    }

    ISerializable <|.. MenuItem
    IPrintable <|.. Order
    IPrintable <|.. Bill
    IDiscountable <|.. Bill
    Order "1" *-- "0..*" OrderItem : owns lines
    Bill "1" *-- "0..*" OrderItem : snapshots lines
    Bill ..> Order : built from
    Payment ..> Bill : recorded from
    OrderItem ..> MenuItem : frozen copy of
    RecipeLine ..> MenuItem : dish
    RecipeLine ..> Ingredient : quantity
    Ingredient ..> Supplier : supplied by
    Order ..> Table : dine-in seat
    Reservation ..> Table : holds
    Promo ..> Bill : discounts
    User ..> Employee : optional link
```

Three deliberate design points visible above:

* `OrderItem` freezes `name` **and** `unitPrice` at order time. Re-pricing the menu, or deleting a
  dish, can never rewrite a printed bill.
* `Bill::settle()` is **private** and `services::BillingService` is its only `friend`. That single
  line in `BillingService::settle()` is the only place in the whole application where a bill can
  become "paid", and it runs *after* the payment row commits.
* `Bill::total()` is `subtotal - discount + serviceCharge`. There is no tax term — anywhere.

---

## 4. `aluchop::persistence` — SQLite and the raw file layer

```mermaid
classDiagram
    direction TB

    class Database {
        -QString m_path
        -QSqlDatabase m_db
        +instance() Database$
        +open(QString dbFilePath) void
        +close() void
        +isOpen() bool
        +handle() QSqlDatabase
        +exec(QString sql) QSqlQuery
        +prepared(QString sql, QVariantList binds) QSqlQuery
        +transaction(function body) void
    }

    class SchemaMigrator {
        -Database m_db
        +kLatestVersion int$
        +migrate(QString menuSeedJsonPath) void
        +currentVersion() int
        -applyMigration1() void
        -seedIfEmpty(QString menuSeedJsonPath) void
    }

    class Repository~T~ {
        <<abstract>>
        #QString m_table
        +Repository(QString tableName)
        +findAll() vector~T~
        +findById(int id) optional~T~
        +count() int
        +removeById(int id) void
        #fromRecord(QSqlRecord rec)* T
        #orderByClause() QString
    }

    class UserRepository {
        +byUsername(QString username) optional~User~
        +byRememberToken(QString token) optional~User~
        +setRememberToken(int userId, QString token) void
        +setPassword(int userId, QString hash, QString salt) void
    }
    class MenuRepository {
        +byCategory(QString category) vector~MenuItem~
        +search(QString term) vector~MenuItem~
        +setAvailability(int itemId, bool available) void
        +recipeFor(int menuItemId) vector~RecipeLine~
        +setRecipe(int menuItemId, vector~RecipeLine~ lines) void
    }
    class CustomerRepository {
        +byPhone(QString phone) optional~Customer~
        +search(QString term) vector~Customer~
    }
    class EmployeeRepository {
        +makeTyped(int employeeId) unique_ptr~Employee~
        +allTyped() vector~unique_ptr~
        +markAttendance(int employeeId, QDate day, QString status, QTime in, QTime out) void
        +attendanceFor(int employeeId, int year, int month) vector~tuple~
    }
    class OrderRepository {
        +insertOrder(Order o) int
        +updateOrder(Order o) void
        +updateStatus(int orderId, OrderStatus s) void
        +loadOrder(int orderId) optional~Order~
        +withStatus(OrderStatus s) vector~Order~
        +activeOrders() vector~Order~
        +between(QDateTime from, QDateTime to) vector~Order~
        +forCustomer(int customerId, int limit) vector~Order~
        +markMergedInto(int sourceOrderId, int targetOrderId) void
    }
    class IngredientRepository {
        +adjustStock(int ingredientId, double deltaQty, QString reason, int refOrderId, Money unitCost, QString note) void
        +lowStock() vector~Ingredient~
        +expiringWithin(int days) vector~Ingredient~
        +history(int ingredientId, int limit) vector~tuple~
    }
    class SupplierRepository
    class TableRepository {
        +activeWithCapacityAtLeast(int guests) vector~Table~
    }
    class ReservationRepository {
        +setStatus(int reservationId, ReservationStatus s) void
        +onDay(QDate day) vector~Reservation~
        +overlapping(int tableId, QDateTime start, int durationMin) vector~Reservation~
    }
    class PaymentRepository {
        +between(QDateTime from, QDateTime to) vector~Payment~
        +revenueBetween(QDateTime from, QDateTime to) Money
        +popularItems(QDateTime from, QDateTime to, int topN) vector~pair~
    }
    class PromoRepository {
        +byCode(QString code) optional~Promo~
    }

    class SettingsRepository {
        +get(QString key, QString fallback) QString
        +set(QString key, QString value) void
        +remove(QString key) void
    }
    class AuditRepository {
        +insert(uint32 seq, int64 tsUtcMs, int userId, QString action, QString entity, Money amount, QString details) void
        +recent(int limit) vector~tuple~
    }

    class AuditRecord {
        <<struct>>
        +int64 timestampUtcMs
        +int64 amountPaisa
        +uint32 magic
        +uint32 seq
        +uint32 userId
        +char action_16
        +char entity_16
        +char details_64
        +uint32 checksum
    }

    class BinaryRecordFile {
        #fstream m_stream
        #QString m_path
        +openOrCreate() void
        +close() void
        +isOpen() bool
        +recordCount() size_t
        +append(AuditRecord rec) void
        +readAt(size_t index) AuditRecord
        +overwriteAt(size_t index, AuditRecord rec) void
        +checksumOf(AuditRecord rec) uint32$
        +fillString(char dest, size_t cap, QString src) void$
        #ensureOpen() void
    }

    class AuditTrail {
        -uint32 m_nextSeq
        +AuditTrail(QString path)
        +record(uint32 userId, QString action, QString entity, Money amount, QString details) uint32
        +at(size_t index) AuditRecord
        +tail(size_t n) vector~AuditRecord~
        +size() size_t
        +verifyIntegrity(size_t firstBadIndex) bool
    }

    class CsvWriter {
        #ofstream m_out
        #QString m_path
        #int m_rows
        +open(QString path) void
        +writeRow(QStringList cells) void
        +close() void
        +isOpen() bool
        +rowsWritten() int
        +escapeCell(QString cell) QString$
    }

    class BackupManager {
        -QString m_dbPath
        -QString m_backupDir
        +createBackup() QString
        +restoreBackup(QString backupFile) void
        +listBackups() vector~QString~
        +isValidSqliteFile(QString path) bool$
    }

    Repository~T~ <|-- UserRepository
    Repository~T~ <|-- MenuRepository
    Repository~T~ <|-- CustomerRepository
    Repository~T~ <|-- EmployeeRepository
    Repository~T~ <|-- OrderRepository
    Repository~T~ <|-- IngredientRepository
    Repository~T~ <|-- SupplierRepository
    Repository~T~ <|-- TableRepository
    Repository~T~ <|-- ReservationRepository
    Repository~T~ <|-- PaymentRepository
    Repository~T~ <|-- PromoRepository

    BinaryRecordFile <|-- AuditTrail : private
    BinaryRecordFile ..> AuditRecord : reads and writes
    Repository~T~ ..> Database : singleton
    SchemaMigrator --> Database : borrows
    SettingsRepository ..> Database : singleton
    AuditRepository ..> Database : singleton
```

`SettingsRepository` and `AuditRepository` deliberately do **not** derive `Repository<T>`: one has
no `id` column at all (`settings` is keyed by `key`), the other is an append-only log rather than
an entity store, so the generic `findAll` / `findById` / `removeById` skeleton buys them nothing.

---

## 5. `aluchop::services` — business rules and the composition root

```mermaid
classDiagram
    direction TB

    class AppContext {
        -DbBootstrap m_bootstrap
        -UserRepository m_userRepo
        -MenuRepository m_menuRepo
        -CustomerRepository m_customerRepo
        -EmployeeRepository m_employeeRepo
        -OrderRepository m_orderRepo
        -IngredientRepository m_ingredientRepo
        -SupplierRepository m_supplierRepo
        -TableRepository m_tableRepo
        -ReservationRepository m_reservationRepo
        -PaymentRepository m_paymentRepo
        -PromoRepository m_promoRepo
        -SettingsRepository m_settingsRepo
        -AuditRepository m_auditRepo
        -AuditTrail m_auditTrail
        -BackupManager m_backups
        +AppContext(QString dataDir, QString menuSeedJsonPath)
        +auth() AuthService
        +menu() MenuService
        +orders() OrderService
        +billing() BillingService
        +customers() CustomerService
        +employees() EmployeeService
        +inventory() InventoryService
        +reservations() ReservationService
        +reports() ReportService
        +settings() SettingsService
        +audit() AuditService
        +notifications() NotificationService
        +commands() CommandStack
    }

    class AuthService {
        +kMinPasswordLength int$
        +login(QString username, QString password, bool rememberMe) Result~User~
        +logout() void
        +currentUser() optional~User~
        +hasRole(UserRole atLeast) bool
        +tryRememberedLogin() optional~User~
        +changePassword(QString oldPassword, QString newPassword) Result~void~
        +securityQuestionFor(QString username) Result~QString~
        +resetPasswordWithAnswer(QString username, QString answer, QString newPassword) Result~void~
        +createUser(QString username, QString password, UserRole role, int employeeId, QString q, QString a) Result~int~
        +hashPassword(QString password, QString salt) QString$
        +generateSalt() QString$
    }

    class OrderService {
        -KitchenQueue m_queue
        +createOrder(OrderType type, int tableId, int customerId, int waiterId) Result~Order~
        +addItem(int orderId, int menuItemId, int qty) Result~void~
        +updateItemQty(int orderId, size_t index, int qty) Result~void~
        +removeItem(int orderId, size_t index) Result~void~
        +cancelOrder(int orderId) Result~void~
        +splitOrder(int orderId, vector~size_t~ itemIndexes) Result~Order~
        +mergeOrders(int targetOrderId, int sourceOrderId) Result~void~
        +submitToKitchen(int orderId) Result~void~
        +advanceStatus(int orderId) Result~void~
        +activeOrders() vector~Order~
        +kitchenQueue() KitchenQueue
        +rebuildQueue() void
    }

    class KitchenQueue {
        -queue~int~ m_q
        +push(int orderId) void
        +front() int
        +pop() void
        +empty() bool
        +size() size_t
        +snapshot() vector~int~
        +remove(int orderId) void
    }

    class BillingService {
        +prepareBill(int orderId, QString promoCode, int serviceChargePercent) Result~Bill~
        +settle(int orderId, Bill bill, PaymentMethod method, Money tendered, int cashierUserId) Result~Payment~
        +changeFor(Money total, Money tendered) Money
        +receiptText(Bill bill) QString
    }

    class InventoryService {
        +all() vector~Ingredient~
        +addIngredient(Ingredient i) Result~int~
        +restock(int ingredientId, double qty, Money unitCost, QString note) Result~void~
        +recordWaste(int ingredientId, double qty, QString note) Result~void~
        +deductForOrder(Order order) void
        +lowStock() vector~Ingredient~
        +expiring(int days) vector~Ingredient~
        +suppliers() vector~Supplier~
    }

    class MenuService {
        +all() vector~MenuItem~
        +search(QString term, QString category, bool availableOnly, MenuSort sort) vector~MenuItem~
        +categories() vector~QString~
        +create(MenuItem item) Result~int~
        +setAvailability(int itemId, bool available) Result~void~
        +recipeFor(int menuItemId) vector~RecipeLine~
        +setRecipe(int menuItemId, vector~RecipeLine~ lines) Result~void~
    }

    class CustomerService {
        +search(QString term) vector~Customer~
        +create(QString name, QString phone, QString email) Result~int~
        +recordVisit(int customerId, Money spent) Result~void~
        +redeemPoints(int customerId, int points) Result~void~
        +visitHistory(int customerId, int limit) vector~Order~
        +favouriteItems(int customerId, int topN) vector~QString~
    }

    class EmployeeService {
        +staff() vector~unique_ptr~
        +hire(QString name, QString phone, QString email, QString position, Money salary, QString shift) Result~int~
        +deactivate(int employeeId) Result~void~
        +markAttendance(int employeeId, QDate day, QString status, QTime in, QTime out) Result~void~
        +payrollPreview() vector~tuple~
        +staffCustomerFor(int customerId) optional~StaffCustomer~
    }

    class ReservationService {
        +availableTables(QDateTime start, int durationMin, int guests) vector~Table~
        +book(Reservation r) Result~int~
        +cancel(int reservationId) Result~void~
        +seat(int reservationId) Result~void~
        +complete(int reservationId) Result~void~
        +onDay(QDate day) vector~Reservation~
    }

    class ReportService {
        +salesForDay(QDate day) Money
        +weeklySales(QDate weekEnding) array~Money~
        +salesForMonth(int year, int month) Money
        +popularItems(int topN) vector~pair~
        +revenueSeries(QDate from, QDate to) vector~pair~
        +customerCount() int
        +pendingOrderCount() int
        +makeReport(ReportKind kind, QDate from, QDate to) unique_ptr~ReportGenerator~
    }

    class SettingsService {
        -map~QString~ m_cache
        +get(QString key, QString fallback) QString
        +set(QString key, QString value) void
        +createBackup() Result~QString~
        +restoreBackup(QString path) Result~void~
        +listBackups() vector~QString~
    }

    class AuditService {
        -int m_activeUserId
        +setActiveUser(int userId) void
        +log(QString action, QString entity) void
        +log(QString action, QString entity, Money amount, QString details) void
        +verifyTrailIntegrity(size_t firstBadIndex) bool
        +trailRecordCount() size_t
        +recentTrailRecords(size_t n) vector~AuditRecord~
    }

    class NotificationService {
        <<QObject>>
        +notify(QString title, QString message, int level) void
        +announceDataChanged(QString domain) void
        +notification(QString title, QString message, int level) signal
        +dataChanged(QString domain) signal
    }

    AppContext *-- AuthService : owns by value
    AppContext *-- MenuService : owns by value
    AppContext *-- OrderService : owns by value
    AppContext *-- BillingService : owns by value
    AppContext *-- CustomerService : owns by value
    AppContext *-- EmployeeService : owns by value
    AppContext *-- InventoryService : owns by value
    AppContext *-- ReservationService : owns by value
    AppContext *-- ReportService : owns by value
    AppContext *-- SettingsService : owns by value
    AppContext *-- AuditService : owns by value
    AppContext *-- NotificationService : owns by value
    OrderService *-- KitchenQueue : owns
    OrderService --> InventoryService : deducts on serve
    OrderService --> CustomerService : records visit
    BillingService --> CustomerService : awards points
    BillingService --> EmployeeService : staff discount
    InventoryService --> MenuRepository : reads recipes
    AuthService --> AuditService : logs
```

### Undo/redo — the Command hierarchy

```mermaid
classDiagram
    direction LR

    class Command {
        <<abstract>>
        +execute()* void
        +undo()* void
        +description()* QString
    }
    class AddOrderItemCommand {
        -int m_orderId
        -int m_menuItemId
        -int m_qty
        -size_t m_addedIndex
        +execute() void
        +undo() void
        +description() QString
    }
    class RemoveOrderItemCommand {
        -int m_orderId
        -size_t m_index
        -int m_menuItemId
        -int m_qty
        +execute() void
        +undo() void
        +description() QString
    }
    class ToggleAvailabilityCommand {
        -int m_itemId
        -bool m_makeAvailable
        +execute() void
        +undo() void
        +description() QString
    }
    class AdjustStockCommand {
        -int m_ingredientId
        -double m_qty
        -Money m_unitCost
        -QString m_note
        +execute() void
        +undo() void
        +description() QString
    }
    class CommandStack {
        -vector~unique_ptr~ m_undo
        -vector~unique_ptr~ m_redo
        +kMaxDepth size_t$
        +run(unique_ptr~Command~ cmd) Result~void~
        +canUndo() bool
        +canRedo() bool
        +undoText() QString
        +redoText() QString
        +undo() Result~void~
        +redo() Result~void~
    }

    Command <|-- AddOrderItemCommand
    Command <|-- RemoveOrderItemCommand
    Command <|-- ToggleAvailabilityCommand
    Command <|-- AdjustStockCommand
    CommandStack o-- Command : owns 50 deep
    AddOrderItemCommand --> OrderService : borrows
    RemoveOrderItemCommand --> OrderService : borrows
    ToggleAvailabilityCommand --> MenuService : borrows
    AdjustStockCommand --> InventoryService : borrows
```

### Reports — protected inheritance of the CSV writer

```mermaid
classDiagram
    direction LR

    class CsvWriter {
        #ofstream m_out
        +open(QString path) void
        +writeRow(QStringList cells) void
        +close() void
    }
    class ReportGenerator {
        <<abstract>>
        +title()* QString
        +header()* QStringList
        +rows()* vector~QStringList~
        +exportCsv(QString outPath) QString
    }
    class SalesReport {
        -QDate m_from
        -QDate m_to
        +title() QString
        +header() QStringList
        +rows() vector~QStringList~
    }
    class InventoryReport {
        +title() QString
        +header() QStringList
        +rows() vector~QStringList~
    }
    class OrdersReport {
        -QDate m_from
        -QDate m_to
        +title() QString
        +header() QStringList
        +rows() vector~QStringList~
    }
    class CustomersReport {
        +title() QString
        +header() QStringList
        +rows() vector~QStringList~
    }
    class EmployeesReport {
        +title() QString
        +header() QStringList
        +rows() vector~QStringList~
    }

    CsvWriter <|-- ReportGenerator : protected
    ReportGenerator <|-- SalesReport
    ReportGenerator <|-- InventoryReport
    ReportGenerator <|-- OrdersReport
    ReportGenerator <|-- CustomersReport
    ReportGenerator <|-- EmployeesReport
    ReportService ..> ReportGenerator : factory
```

---

## 6. `aluchop::gui` — Qt Widgets presentation

Every class here carries `Q_OBJECT`. **No GUI translation unit names an `aluchop::persistence`
type or contains a line of SQL** — that is what makes "the GUI never touches the database"
mechanically checkable rather than a promise.

```mermaid
classDiagram
    direction TB

    class MainWindow {
        <<QMainWindow>>
        -Sidebar m_sidebar
        -QStackedWidget m_stack
        -array~Page~ m_pages
        -QLabel m_footer
        -ToastHost m_toasts
        -CommandPalette m_palette
        +MainWindow(AppContext ctx, QWidget parent)
        -onNavigate(int pageIndex) void
        -onDataChanged(QString domain) void
        -onNotification(QString title, QString message, int level) void
        -onUndo() void
        -onRedo() void
        -onToggleTheme() void
        -onOpenPalette() void
        -buildShortcuts() void
        -buildFooter() void
    }

    class Page {
        <<abstract>>
        #AppContext m_ctx
        +pageTitle()* QString
        +refresh()* void
    }

    class DashboardPage {
        -array~StatCard~ m_cards
        -QChartView m_revenueChart
        -QListWidget m_alerts
        -QTableWidget m_pendingOrders
        -QListWidget m_popularItems
        +pageTitle() QString
        +refresh() void
        -animateCards() void
    }
    class MenuPage {
        +pageTitle() QString
        +refresh() void
    }
    class OrdersPage {
        +pageTitle() QString
        +refresh() void
        +onNewOrder() void
    }
    class CustomersPage {
        +pageTitle() QString
        +refresh() void
    }
    class EmployeesPage {
        +pageTitle() QString
        +refresh() void
    }
    class InventoryPage {
        +pageTitle() QString
        +refresh() void
    }
    class ReservationsPage {
        +pageTitle() QString
        +refresh() void
    }
    class ReportsPage {
        +pageTitle() QString
        +refresh() void
    }
    class SettingsPage {
        +pageTitle() QString
        +refresh() void
    }

    class Sidebar {
        -vector~QToolButton~ m_buttons
        -int m_active
        +addEntry(QString iconSvgPath, QString label) void
        +setActive(int index) void
        +navigate(int index) signal
    }

    class ThemeManager {
        <<QObject>>
        -Mode m_mode
        +kLight Palette$
        +kDark Palette$
        +instance() ThemeManager$
        +mode() Mode
        +setMode(Mode m) void
        +toggle() void
        +palette() Palette
        +styleSheet() QString
        +apply(QApplication app) void
        +themeChanged() signal
    }

    class Palette {
        <<struct>>
        +QColor primary
        +QColor secondary
        +QColor accent
        +QColor background
        +QColor card
        +QColor border
        +QColor success
        +QColor danger
        +QColor text
        +QColor textMuted
        +QColor hover
        +QColor shadow
    }

    class LoginWindow {
        <<QWidget>>
        +loggedIn() signal
        -onLoginClicked() void
        -onForgotPassword() void
    }
    class SplashScreen {
        <<QSplashScreen>>
        +showFor(int ms, function then) void
    }
    class LoadingOverlay {
        <<QFrame, unused>>
        +setMessage(QString message) void
        +begin(QString message) void
        +end() void
    }
    class BillingDialog {
        <<QDialog>>
        -Bill m_bill
        -int m_orderId
        -onApplyPromo() void
        -onMethodChanged() void
        -onTenderedEdited() void
        -onSettle() void
        -onPrintReceipt() void
        -updateTotals() void
    }
    class CommandPalette {
        <<QDialog>>
        +navigateRequested(int pageIndex) signal
        +openOrderRequested(int orderId) signal
        -onQueryChanged(QString q) void
        -onActivated() void
    }
    class StatCard {
        <<QFrame>>
        +setValue(QString value) void
        +setDelta(QString delta, bool positive) void
        +animateIn(int delayMs) void
    }
    class Toast {
        <<QFrame>>
        +popIn(int lifetimeMs) void
        +dismiss() void
        +dismissed(Toast self) signal
    }
    class ToastHost {
        <<QWidget>>
        -vector~Toast~ m_toasts
        +kMaxVisible size_t$
        +show(QString title, QString message, int level, int ms) void
    }
    class PdfExporter {
        <<utility>>
        +exportReportPdf(QString title, QStringList header, vector~QStringList~ rows, QString outPath) Result~QString~$
        +receiptPdf(Bill bill, QString outPath) Result~QString~$
        +printReceipt(Bill bill, QWidget parent) void$
    }

    Page <|-- DashboardPage
    Page <|-- MenuPage
    Page <|-- OrdersPage
    Page <|-- CustomersPage
    Page <|-- EmployeesPage
    Page <|-- InventoryPage
    Page <|-- ReservationsPage
    Page <|-- ReportsPage
    Page <|-- SettingsPage

    MainWindow *-- Sidebar : owns
    MainWindow *-- ToastHost : owns
    MainWindow *-- CommandPalette : owns
    MainWindow o-- Page : nine pages
    MainWindow --> AppContext : borrows
    Page --> AppContext : borrows
    DashboardPage *-- StatCard : four cards
    ToastHost *-- Toast : owns
    OrdersPage ..> BillingDialog : opens
    BillingDialog ..> PdfExporter : receipt
    ReportsPage ..> PdfExporter : report PDF
    ThemeManager *-- Palette : light and dark
    LoginWindow --> AppContext : borrows
```

### Reusable widget vocabulary — `gui/Widgets.hpp`

A header-only set of house-styled widgets shared by every page. *(Not listed in the frozen
`ARCHITECTURE.md` file manifest — it was added during implementation; see the drift note in the
README.)*

```mermaid
classDiagram
    direction LR
    class GlassPanel {
        <<QFrame>>
        +applyShadow(int blurRadius, int yOffset, qreal opacity) void
    }
    class ElegantTable {
        <<QTableWidget>>
        +clearRows() void
        -applyHouseStyle() void
    }
    class SearchBar {
        <<QLineEdit>>
    }
    class ChartCard {
        <<QFrame>>
        +setChart(QChart chart) void
        +view() QChartView
        +setTitle(QString title) void
    }
    class EmptyState {
        <<QFrame>>
        +setText(QString title, QString hint) void
        +enableAction(QString label) QPushButton
    }
    GlassPanel ..> ThemeManager : shadow colour
    ChartCard *-- QChartView : owns
```

---

## 7. Cross-layer dependency rule

```mermaid
flowchart TD
    GUI["aluchop::gui — Widgets, Charts, Svg, PrintSupport"]
    SVC["aluchop::services — business rules, QtCore only"]
    PER["aluchop::persistence — QtSql plus fstream"]
    MOD["aluchop::models — domain entities, no I/O"]
    COR["aluchop::core — Money, Result, exceptions, Logger"]

    GUI --> SVC
    GUI --> MOD
    GUI --> COR
    SVC --> PER
    SVC --> MOD
    SVC --> COR
    PER --> MOD
    PER --> COR
    MOD --> COR
```

Two greppable rules enforce it, and both come back empty on the shipped tree:

```bash
# no SQL in the services layer
grep -rnE 'QSqlQuery|QSqlDatabase|QSqlRecord|SELECT |INSERT |UPDATE |DELETE ' \
     src/services include/aluchop/services

# no persistence type ever named by the GUI
grep -rnE 'persistence::|aluchop/persistence/' src/gui include/aluchop/gui src/main.cpp
```

---

<sub>© 2026 AluChop Restaurant Management System. Developed by Shashank Bhattarai (ACE082BCT078).
For academic use as an ENCT151 Object-Oriented Programming coursework project. All rights reserved.</sub>

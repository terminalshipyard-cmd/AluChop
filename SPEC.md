# AluChop — Restaurant Management System
## Authoritative Build Specification (ENCT151 OOP Coursework)

> This file is the SINGLE SOURCE OF TRUTH. It is distilled from `OOP Project C++ Prompt.pdf`.
> Every agent working on this project MUST read this file first and MUST NOT contradict it.

---

## 0. NON-NEGOTIABLES

| Rule | Detail |
|---|---|
| Language | C++17 (or newer). Modern C++ only. |
| GUI | **Qt 6** (Widgets). Installed via Homebrew: `qtbase qtcharts qtsvg qttools`. |
| Build | CMake ≥ 3.20, Ninja generator. Must build clean with **zero errors**. |
| Persistence | **SQLite** (via Qt SQL `QSQLITE` driver) **AND** demonstrable raw-C++ file handling (see §5). |
| Currency | **NPR** only. Prices are **already tax-inclusive** — **NEVER add tax on top**. |
| Memory | No leaks. RAII. Smart pointers where appropriate. |
| Architecture | Layered: `models / persistence / services / gui`. GUI must never touch SQL directly. |
| Attribution | Footer + README credit **Shashank Bhattarai, ACE082BCT078, shashankbhattarai006@gmail.com**. |
| Commits | Author `nlethetech`. **NEVER** add "Co-Authored-By: Claude" or "Generated with Claude". |

---

## 1. DESIGN SYSTEM — Sage Green

Exact palette (do not improvise):

```
Primary     #5D7A66
Secondary   #7E9B84
Accent      #A8C3A1
Background  #F6F8F4
Cards       #FFFFFF
Borders     #D7E4D2
Success     #5E9E66
Danger      #D16464
Text        #1F2D1F
```

Dark mode is a required extra feature — derive a dark counterpart set from the same hues
(deep desaturated green-greys, never pure black; keep Primary/Accent recognisable).

Visual language: minimalistic, premium, modern. Inspiration = modern POS software,
restaurant dashboards, Apple HIG, clean Material, glassmorphism.

Required visual traits:
- Rounded corners (8–16px radius)
- Soft shadows (`QGraphicsDropShadowEffect`, low opacity, generous blur)
- Glassmorphism where appropriate (translucent panels over tinted backdrops)
- Smooth animations (`QPropertyAnimation` / `QGraphicsOpacityEffect`)
- Sidebar navigation with minimal icons
- Dashboard widgets + statistics cards
- Elegant tables (no harsh gridlines; zebra or spaced rows; generous row height)
- Search bars
- Clean typography, plenty of whitespace, professional spacing
- **Avoid clutter.**

Theming is implemented through a `ThemeManager` that generates Qt Style Sheets (QSS) at
runtime from a palette struct, so Light/Dark switch live without restart.

---

## 2. MENU

The menu is seeded from `assets/menu/menu_seed.json` and loaded into SQLite on first run.
Prices in NPR, **tax-inclusive**.

Required categories (all 14 must be present and populated):

`Sushi`, `Pizza`, `Pasta`, `Main Course`, `Dimsum`, `From the Tandoor`, `From the Wok`,
`Bread & Rice`, `Dessert`, `Drinks`, `Beer`, `Wine`, `Mocktails`, `Shots`

Each item carries: name, category, price (NPR), description, availability flag,
optional image path, and a recipe (list of ingredient + quantity) used by Inventory.

---

## 3. FEATURE SET (all required)

### Authentication
Admin login · Employee login · Forgot password · Remember login · Role-based authorisation.
Passwords stored **salted + hashed** (`QCryptographicHash` SHA-256 + per-user salt), never plaintext.

### Dashboard
Daily sales · Weekly sales · Monthly sales · Popular items · Inventory alerts ·
Reservations · Pending orders · Revenue graphs (QtCharts) · Customer count.
Animated stat cards on load.

### Restaurant Menu
Browse · Search · Sort · Filter · Categories · Item images · Descriptions · Pricing · Availability toggle.

### Order Management
Create · Edit · Delete order · Dine-In / Takeaway / Delivery order types ·
Split bills · Merge bills · Order queue · Kitchen status (Pending→Preparing→Ready→Served) ·
Generate bill · Print receipt.

### Customer Management
Customer database · Phone · Email · Loyalty points · Visit history · Favourite orders.

### Employee Management
Staff information · Salary · Attendance · Position · Shift · Performance.

### Inventory
Ingredients · Stock · Supplier · Low-stock alerts · Restocking · Expiry dates · Inventory usage
(deducted automatically from item recipes when an order is served).

### Reservation System
Table booking · Time · Guests · Special requests · Availability.

### Billing
Invoice generation · Receipt · Discounts · Promo codes · Service charges ·
**Tax included (never added)** · Cash / Card / Digital Wallet · Change calculation.

### Reports
Sales · Inventory · Orders · Revenue · Customers · Employees · Charts · CSV export · PDF export.

### Settings
Restaurant information · Themes (Light/Dark) · Database backup · Restore · Export.

### Extra Features
Dark mode · Light mode · Search everywhere (global palette) · Keyboard shortcuts ·
Undo · Redo · Notifications · Auto-save · Settings · Recent orders · Sales charts ·
Animated dashboard · Loading screen · Splash screen · Professional icons.
(Sound effects optional — skip.)

---

## 4. PERSISTENCE

SQLite tables (minimum): `users`, `customers`, `employees`, `menu_items`, `ingredients`,
`recipes`, `suppliers`, `orders`, `order_items`, `reservations`, `tables`, `payments`,
`inventory_transactions`, `settings`, `promos`, `audit_log`.

Backup = file copy of the `.db` + a timestamped export. Restore = validated swap-in.

---

## 5. OOP SYLLABUS COVERAGE — THE MOST IMPORTANT PART

Every item below **must appear naturally** in the codebase — never as a contrived demo class.
Each must be marked in-code with a Doxygen tag so it is greppable and defensible in a viva:

```cpp
/// @oop-concept Operator Overloading :: operator+= merges two bills
```

A machine-checkable matrix lives at `docs/OOP_COVERAGE.md` mapping **concept → file:line → why it is natural there**.

### 5.1 C++ Basics
Functions · Function overloading · Inline functions · Default arguments · Pass by reference ·
Return by reference · Arrays · Strings · Pointers · Dynamic memory allocation · Structures ·
Enumerations · Namespaces · Constants.

### 5.2 Objects & Classes
Multiple classes · Objects · Constructors · Destructor · Copy constructor ·
Parameterised constructor · Objects as members · Object arrays · Object pointers ·
Dynamic objects · Static members · Constant objects · Constant member functions ·
Friend functions · Friend classes.

### 5.3 Operator Overloading
`+` · `-` · `==` · `<` · `<<` · `[]` · assignment (`=`) · increment (`++`) · plus any other practical operators.

### 5.4 Inheritance
Single · Multiple · Hierarchical · Multilevel · Hybrid · Public · Protected · Private ·
**Virtual base class** (required — solve a real diamond) · Method overriding.

### 5.5 Polymorphism
Virtual functions · Pure virtual functions · Abstract classes · Runtime polymorphism ·
Compile-time polymorphism (overloading + templates).

### 5.6 File Handling
Read · Write · Append · **Binary files** · **ASCII files** · **Random access** ·
Sequential access · Error checking.
> SQLite alone does NOT satisfy this. Implement a real `<fstream>` layer:
> binary fixed-record audit/transaction log with `seekg`/`seekp` random access,
> CSV/text sequential export, append-mode logging.

### 5.7 Templates
Function templates · Class templates · STL · `vector` · `map` · `queue` · algorithms · iterators.
> e.g. a `Repository<T>` class template, a generic `Result<T>`, a `KitchenQueue` over `std::queue`.

### 5.8 Exception Handling
`try` · `catch` · `throw` · multiple catch · **rethrow** · custom exception hierarchy
(`AluChopException` base → `DatabaseException`, `ValidationException`, `AuthException`,
`InventoryException`, `FileIOException`).

---

## 6. CLASS STRUCTURE (suggested — expand as needed)

Restaurant · Menu · MenuItem · Inventory · Ingredient · Supplier · Customer · Employee ·
Manager · Chef · Waiter · Admin · Order · OrderItem · Bill · Invoice · Reservation ·
Payment · Table · Analytics · Authentication · Database · FileManager · Logger · Settings ·
ThemeManager · ReportGenerator · Notification · Dashboard.

**Inheritance design (must satisfy every inheritance form in §5.4):**
- `Person` (abstract base)
  - `Employee : virtual public Person` — **virtual base**
  - `Customer : virtual public Person`
- `Waiter`, `Chef` : `public Employee` — hierarchical
- `Manager : public Employee` — single/multilevel
- `Admin : public Manager, public IAuditable` — multiple + hybrid
- Interfaces as pure-abstract mixins: `IPrintable`, `ISerializable`, `IAuditable`, `IDiscountable`.

---

## 7. SOFTWARE ENGINEERING

Header files (`include/`) · source files (`src/`) · namespaces (`aluchop::models`,
`aluchop::persistence`, `aluchop::services`, `aluchop::gui`) · layered architecture ·
separate GUI / logic / database / models · meaningful comments · **Doxygen-style comments** ·
consistent naming · proper encapsulation · modular code.

---

## 8. PROJECT STRUCTURE

```
AluChop/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── SPEC.md
├── include/aluchop/{models,persistence,services,gui,core}/*.hpp
├── src/{models,persistence,services,gui,core}/*.cpp
├── src/main.cpp
├── assets/{fonts,images,icons,menu}/
├── reports/          (generated CSV/PDF output)
├── exports/          (generated backups/exports)
├── docs/             (UML, flowchart, use-case, ER, OOP_COVERAGE.md)
└── tests/            (optional smoke tests)
```

---

## 9. DOCUMENTATION DELIVERABLES

- `README.md` — overview, features, installation, dependencies, screenshots placeholder,
  folder structure, class diagram, UML, OOP concepts used, future improvements, credits.
- `docs/UML_CLASS_DIAGRAM.md` — Mermaid `classDiagram`
- `docs/FLOWCHART.md` — Mermaid flowchart of order lifecycle
- `docs/USE_CASE.md` — Mermaid use-case diagram
- `docs/ER_DIAGRAM.md` — Mermaid `erDiagram` of the SQLite schema
- `docs/CLASS_RELATIONSHIPS.md` — prose explanation of every relationship
- `docs/OOP_COVERAGE.md` — the §5 matrix, concept → file:line → justification

---

## 10. FINAL TOUCH

Subtle footer at the bottom of the main window, elegant typography:

```
Designed & Developed by
Shashank Bhattarai
ACE082BCT078
shashankbhattarai006@gmail.com
```

Copyright line:

> © 2026 AluChop Restaurant Management System. Developed by Shashank Bhattarai (ACE082BCT078).
> For academic use as an ENCT151 Object-Oriented Programming coursework project. All rights reserved.

---

## 11. DEFINITION OF DONE

1. `cmake --build build` completes with **zero errors**.
2. The app **launches**, shows a splash screen, and reaches the login window.
3. Login as admin works; every sidebar screen opens without crashing.
4. A full order can be created → billed → paid → receipt generated → inventory deducted.
5. Every §5 concept is present, tagged, and listed in `docs/OOP_COVERAGE.md`.
6. No memory leaks on a clean exit path; RAII throughout.
7. All docs in §9 exist and are accurate to the real code.

# AluChop — Verified Toolchain

**This was smoke-tested end-to-end on this machine before any code was written. Do not deviate.**

| Component | Value |
|---|---|
| Compiler | Apple clang 17.0.0 (arm64-apple-darwin24.6.0) |
| Standard | C++17 |
| CMake | 4.3.3 |
| Generator | Ninja (`/opt/homebrew/bin/ninja`) |
| Qt | 6.11.1 (Homebrew, **split formulae**) |
| SQL driver | `QSQLITE` — confirmed present in `QSqlDatabase::drivers()` |

## CRITICAL: Homebrew splits Qt into separate formulae

`find_package(Qt6 COMPONENTS Charts ...)` looks for addon configs under **qtbase's** prefix and
will **fail** with `Expected Config file at .../qtbase/lib/cmake/Qt6Charts/... does NOT exist`.

The fix is Qt's official override variable — note it is **`PACKAGES`, plural**
(`QT_ADDITIONAL_PACKAGE_PREFIX_PATH` singular is wrong and silently does nothing):

```
QT_ADDITIONAL_PACKAGES_PREFIX_PATH
```

## The exact, verified configure + build commands

```bash
cd ~/LocalProjects/AluChop

cmake -B build -G Ninja \
  -DCMAKE_PREFIX_PATH='/opt/homebrew/opt/qtbase' \
  -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH='/opt/homebrew/opt/qtcharts;/opt/homebrew/opt/qtsvg;/opt/homebrew/opt/qttools'

cmake --build build
```

A convenience wrapper `./build.sh` wraps exactly this. Prefer it.

## Verified-available Qt components

`Widgets` · `Sql` · `Charts` · `Svg` · `PrintSupport` · `Core` · `Gui` · `Network` · `Concurrent`

`PrintSupport` is present (Cups found) — use `QPrinter`/`QPdfWriter` for **PDF export** and
**receipt printing**. Do **not** pull in any third-party PDF library.

## NOT available — do not use

- **QtWebEngine** (not installed, huge) — no web views.
- **QML / Qt Quick** — this is a **Qt Widgets** application. Do not write QML.
- **Vulkan** headers (absent; irrelevant — Qt falls back to Metal/OpenGL).

## Single-file syntax check (use this before declaring a file done)

> **CRITICAL — Homebrew Qt 6.11.1 is a *framework* build.**
> `/opt/homebrew/opt/qtbase/include` is essentially EMPTY (2 stray dirs). The real headers live in
> `/opt/homebrew/opt/qtbase/lib/<Module>.framework/Headers`. Any recipe using `-I .../include/QtCore`
> fails on the very first `#include <QString>`. Use the framework paths below.

Builders can validate one translation unit without the whole tree existing yet:

```bash
QB=/opt/homebrew/opt/qtbase/lib
QC=/opt/homebrew/opt/qtcharts/lib
QS=/opt/homebrew/opt/qtsvg/lib

clang++ -fsyntax-only -std=c++17 -Wall -Wextra \
  -I include \
  -F $QB -F $QC -F $QS \
  -I $QB/QtCore.framework/Headers \
  -I $QB/QtGui.framework/Headers \
  -I $QB/QtWidgets.framework/Headers \
  -I $QB/QtSql.framework/Headers \
  -I $QB/QtPrintSupport.framework/Headers \
  -I $QC/QtCharts.framework/Headers \
  -I $QS/QtSvg.framework/Headers \
  -I $QS/QtSvgWidgets.framework/Headers \
  src/path/to/File.cpp
```

A ready-made wrapper exists — prefer it: `./syntax-check.sh src/path/to/File.cpp`

Note: files using `Q_OBJECT` will report missing `moc_*.cpp` at **link** time only —
`-fsyntax-only` is still valid for them.

## Qt gotchas that will bite

1. Any class with signals/slots needs `Q_OBJECT` **and** must be reachable by AUTOMOC
   (`set(CMAKE_AUTOMOC ON)`). `Q_OBJECT` in a `.cpp` needs `#include "File.moc"` at the bottom.
2. `QSqlDatabase` connections are **per-thread**. Keep all DB access on the GUI thread
   or open a named connection per thread. Never copy a `QSqlDatabase` across threads.
3. Qt 6 removed `QString::SkipEmptyParts` → use `Qt::SkipEmptyParts`.
4. Qt 6 `QDateTime::fromString` is strict — pass explicit formats.
5. `qAsConst` is deprecated → use `std::as_const`.
6. Prefer `QStringLiteral` / `u"..."_s` for hot-path strings.
7. Money: **never use `double` for currency.** Store NPR as integer paisa (`long long`)
   or a dedicated `Money` value class. Format only at the presentation edge.

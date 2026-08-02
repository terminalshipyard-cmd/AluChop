# ============================================================================
#  AluChop — Restaurant Management System
#  ENCT151 Object-Oriented Programming coursework
#  Developed by Shashank Bhattarai (ACE082BCT078)
#
#  Build:  ./build.sh          (preferred — wraps the verified toolchain)
#  Manual: see TOOLCHAIN.md
# ============================================================================

cmake_minimum_required(VERSION 3.20)

project(AluChop
    VERSION 1.0.0
    DESCRIPTION "AluChop Restaurant Management System"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language standard — C++17, no compiler extensions.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Qt code generators. AUTOMOC handles Q_OBJECT, AUTORCC handles .qrc,
# AUTOUIC handles .ui files (none today, but harmless and future-proof).
# ---------------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---------------------------------------------------------------------------
# Homebrew ships Qt 6 as SPLIT FORMULAE (see TOOLCHAIN.md).
# Addon modules (Charts/Svg/Tools) do NOT live under qtbase's prefix, so
# find_package() needs QT_ADDITIONAL_PACKAGES_PREFIX_PATH — note PACKAGES,
# plural; the singular spelling silently does nothing.
#
# build.sh always passes these on the command line. The block below is only a
# convenience fallback so a bare `cmake -B build` still configures on this
# machine; anything the user supplies wins.
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_PREFIX_PATH AND EXISTS "/opt/homebrew/opt/qtbase")
    set(CMAKE_PREFIX_PATH "/opt/homebrew/opt/qtbase"
        CACHE STRING "Qt6 base prefix" FORCE)
    message(STATUS "AluChop: defaulted CMAKE_PREFIX_PATH to ${CMAKE_PREFIX_PATH}")
endif()

if(APPLE AND NOT QT_ADDITIONAL_PACKAGES_PREFIX_PATH AND EXISTS "/opt/homebrew/opt/qtcharts")
    set(QT_ADDITIONAL_PACKAGES_PREFIX_PATH
        "/opt/homebrew/opt/qtcharts;/opt/homebrew/opt/qtsvg;/opt/homebrew/opt/qttools"
        CACHE STRING "Qt6 addon prefixes (Homebrew split formulae)" FORCE)
    message(STATUS "AluChop: defaulted QT_ADDITIONAL_PACKAGES_PREFIX_PATH to ${QT_ADDITIONAL_PACKAGES_PREFIX_PATH}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Charts Svg PrintSupport)

message(STATUS "AluChop: building against Qt ${Qt6_VERSION}")

# ---------------------------------------------------------------------------
# Sources — EXPLICIT list, mirroring docs/ARCHITECTURE.md §2 (file manifest).
# Deliberately NOT file(GLOB): globbing is evaluated at configure time only,
# so newly added files are silently missed until someone reconfigures.
# ---------------------------------------------------------------------------

# --- Layer: core (Money, Result, exceptions, logger, algorithms) ------------
set(ALUCHOP_CORE_SOURCES
    src/core/Money.cpp
    src/core/Logger.cpp
)

# --- Layer: models (domain entities — no I/O) -------------------------------
set(ALUCHOP_MODEL_SOURCES
    src/models/Enums.cpp
    src/models/Person.cpp
    src/models/Employee.cpp
    src/models/Customer.cpp
    src/models/Waiter.cpp
    src/models/Chef.cpp
    src/models/Manager.cpp
    src/models/Admin.cpp
    src/models/StaffCustomer.cpp
    src/models/User.cpp
    src/models/MenuItem.cpp
    src/models/OrderItem.cpp
    src/models/Order.cpp
    src/models/Bill.cpp
    src/models/Payment.cpp
    src/models/Ingredient.cpp
    src/models/Supplier.cpp
    src/models/Reservation.cpp
    src/models/Table.cpp
    src/models/Promo.cpp
)

# --- Layer: persistence (SQLite repositories + raw <fstream> file layer) ----
set(ALUCHOP_PERSISTENCE_SOURCES
    src/persistence/Database.cpp
    src/persistence/SchemaMigrator.cpp
    src/persistence/UserRepository.cpp
    src/persistence/MenuRepository.cpp
    src/persistence/CustomerRepository.cpp
    src/persistence/EmployeeRepository.cpp
    src/persistence/OrderRepository.cpp
    src/persistence/IngredientRepository.cpp
    src/persistence/SupplierRepository.cpp
    src/persistence/TableRepository.cpp
    src/persistence/ReservationRepository.cpp
    src/persistence/PaymentRepository.cpp
    src/persistence/PromoRepository.cpp
    src/persistence/SettingsRepository.cpp
    src/persistence/AuditRepository.cpp
    src/persistence/BinaryRecordFile.cpp
    src/persistence/AuditTrail.cpp
    src/persistence/CsvWriter.cpp
    src/persistence/BackupManager.cpp
)

# --- Layer: services (business rules — no SQL text, no widgets) -------------
set(ALUCHOP_SERVICE_SOURCES
    src/services/AppContext.cpp
    src/services/AuthService.cpp
    src/services/AuditService.cpp
    src/services/MenuService.cpp
    src/services/OrderService.cpp
    src/services/BillingService.cpp
    src/services/CustomerService.cpp
    src/services/EmployeeService.cpp
    src/services/InventoryService.cpp
    src/services/ReservationService.cpp
    src/services/ReportService.cpp
    src/services/ReportGenerator.cpp
    src/services/SettingsService.cpp
    src/services/NotificationService.cpp
    src/services/Commands.cpp
)

# --- Layer: gui (Qt Widgets / Charts / Svg / PrintSupport) ------------------
set(ALUCHOP_GUI_SOURCES
    src/gui/ThemeManager.cpp
    src/gui/SplashScreen.cpp
    src/gui/LoginWindow.cpp
    src/gui/MainWindow.cpp
    src/gui/Sidebar.cpp
    src/gui/Page.cpp
    src/gui/DashboardPage.cpp
    src/gui/MenuPage.cpp
    src/gui/OrdersPage.cpp
    src/gui/CustomersPage.cpp
    src/gui/EmployeesPage.cpp
    src/gui/InventoryPage.cpp
    src/gui/ReservationsPage.cpp
    src/gui/ReportsPage.cpp
    src/gui/SettingsPage.cpp
    src/gui/BillingDialog.cpp
    src/gui/StatCard.cpp
    src/gui/Toast.cpp
    src/gui/CommandPalette.cpp
    src/gui/PdfExporter.cpp
)

# ---------------------------------------------------------------------------
# Headers that declare Q_OBJECT must be listed in the target's sources so
# AUTOMOC scans them (ARCHITECTURE.md §11): every gui widget header plus
# services/NotificationService.hpp. Listing the remaining headers as well is
# harmless — AUTOMOC skips those without Q_OBJECT — and it makes the whole
# project visible in IDE project trees.
# ---------------------------------------------------------------------------
set(ALUCHOP_MOC_HEADERS
    include/aluchop/services/NotificationService.hpp
    include/aluchop/gui/ThemeManager.hpp
    include/aluchop/gui/SplashScreen.hpp
    include/aluchop/gui/LoginWindow.hpp
    include/aluchop/gui/MainWindow.hpp
    include/aluchop/gui/Sidebar.hpp
    include/aluchop/gui/Page.hpp
    include/aluchop/gui/DashboardPage.hpp
    include/aluchop/gui/MenuPage.hpp
    include/aluchop/gui/OrdersPage.hpp
    include/aluchop/gui/CustomersPage.hpp
    include/aluchop/gui/EmployeesPage.hpp
    include/aluchop/gui/InventoryPage.hpp
    include/aluchop/gui/ReservationsPage.hpp
    include/aluchop/gui/ReportsPage.hpp
    include/aluchop/gui/SettingsPage.hpp
    include/aluchop/gui/BillingDialog.hpp
    include/aluchop/gui/StatCard.hpp
    include/aluchop/gui/Toast.hpp
    include/aluchop/gui/CommandPalette.hpp
    include/aluchop/gui/PdfExporter.hpp
)

add_executable(AluChop
    src/main.cpp
    ${ALUCHOP_CORE_SOURCES}
    ${ALUCHOP_MODEL_SOURCES}
    ${ALUCHOP_PERSISTENCE_SOURCES}
    ${ALUCHOP_SERVICE_SOURCES}
    ${ALUCHOP_GUI_SOURCES}
    ${ALUCHOP_MOC_HEADERS}
)

target_include_directories(AluChop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(AluChop PRIVATE
    Qt6::Widgets
    Qt6::Sql
    Qt6::Charts
    Qt6::Svg
    Qt6::PrintSupport
)

# ---------------------------------------------------------------------------
# Warnings: loud, but never fatal (-Werror is deliberately NOT enabled).
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(AluChop PRIVATE /W4)
else()
    target_compile_options(AluChop PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Plain command-line executable, not a macOS .app bundle: the runtime resolves
# assets/, reports/ and exports/ relative to the working directory, which the
# post-build copy below mirrors next to the binary.
# ---------------------------------------------------------------------------
set_target_properties(AluChop PROPERTIES
    MACOSX_BUNDLE OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ---------------------------------------------------------------------------
# Verification targets (EXCLUDE_FROM_ALL — never slow down a normal build).
#
#   aluchop_uishot : renders every screen offscreen to PNG. macOS refuses
#                    screencapture without a Screen Recording TCC grant, so the
#                    only reliable way to see the real UI is to have the app
#                    grab its own widgets via QWidget::grab().
#   aluchop_e2e    : headless end-to-end assertion of the business flow
#                    (order -> kitchen -> serve -> inventory -> bill -> pay).
#
# Both reuse the application sources minus main.cpp and supply their own main().
#
#   cmake --build build --target aluchop_uishot aluchop_e2e
# ---------------------------------------------------------------------------
set(ALUCHOP_SHARED_SOURCES
    ${ALUCHOP_CORE_SOURCES}
    ${ALUCHOP_MODEL_SOURCES}
    ${ALUCHOP_PERSISTENCE_SOURCES}
    ${ALUCHOP_SERVICE_SOURCES}
    ${ALUCHOP_GUI_SOURCES}
    ${ALUCHOP_MOC_HEADERS}
)

foreach(_probe uishot e2e)
    if(_probe STREQUAL "uishot")
        set(_probe_src "${CMAKE_CURRENT_SOURCE_DIR}/tools/uishot.cpp")
    else()
        set(_probe_src "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e_test.cpp")
    endif()

    # Only declare the target once its driver file actually exists, so a fresh
    # clone that has not written them yet still configures cleanly.
    if(EXISTS "${_probe_src}")
        add_executable(aluchop_${_probe} EXCLUDE_FROM_ALL
            "${_probe_src}"
            ${ALUCHOP_SHARED_SOURCES}
        )
        target_include_directories(aluchop_${_probe} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(aluchop_${_probe} PRIVATE
            Qt6::Widgets Qt6::Sql Qt6::Charts Qt6::Svg Qt6::PrintSupport
        )
        if(NOT MSVC)
            target_compile_options(aluchop_${_probe} PRIVATE -Wall -Wextra)
        endif()
        set_target_properties(aluchop_${_probe} PROPERTIES
            MACOSX_BUNDLE OFF
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        )
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Assets: copy assets/ next to the binary so the app can be run straight out of
# build/. This is its own ALL target rather than a POST_BUILD step, because
# editing only assets/menu/menu_seed.json does not relink AluChop — a
# POST_BUILD step would then never fire and build/assets would go stale.
# reports/ and exports/ are created empty: the report and backup services write
# into them at runtime.
# ---------------------------------------------------------------------------
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.26")
    set(ALUCHOP_COPY_DIR_CMD copy_directory_if_different)   # skips unchanged files
else()
    set(ALUCHOP_COPY_DIR_CMD copy_directory)
endif()

add_custom_target(AluChopAssets ALL
    COMMAND ${CMAKE_COMMAND} -E ${ALUCHOP_COPY_DIR_CMD}
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:AluChop>/assets"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AluChop>/reports"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AluChop>/exports"
    COMMENT "AluChop: staging assets/ and runtime output folders next to the binary"
    VERBATIM
)
add_dependencies(AluChopAssets AluChop)

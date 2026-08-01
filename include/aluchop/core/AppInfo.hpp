#pragma once
/**
 * @file AppInfo.hpp
 * @brief Immutable application identity, credits, category list and core tuning constants.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Everything here is compile-time constant and has exactly one definition in
 * the whole program (C++17 `inline` variables), so the footer, the About box,
 * printed receipts, exported PDFs and the README all quote the *same* strings.
 */

#include <array>
#include <cstdint>

namespace aluchop::core {

/**
 * @brief Plain aggregate describing who built this application and what it is.
 *
 * No invariants, no behaviour — a struct is the honest choice here.
 */
/// @oop-concept Structures :: plain aggregate for immutable app metadata
struct AppInfo {
    const char* appName;   ///< Full product name.
    const char* version;   ///< Semantic version string.
    const char* developer; ///< Author's full name.
    const char* rollNo;    ///< Author's college roll number.
    const char* email;     ///< Author's contact email.
};

/**
 * @brief The single, immutable identity of this build.
 *
 * Consumed by MainWindow's footer, the About dialog, receipt headers and PDF
 * report footers.
 */
/// @oop-concept Constant Objects :: compile-time app identity, used by footer, About and receipts
inline const AppInfo kAppInfo{
    "AluChop Restaurant Management System", "1.0.0",
    "Shashank Bhattarai", "ACE082BCT078", "shashankbhattarai006@gmail.com"
};

/**
 * @brief The four-line credit block shown in the main window footer (SPEC §10).
 *
 * Newline-separated; render with a QLabel whose alignment is centred.
 */
inline constexpr const char* kAttributionBlock =
    "Designed & Developed by\n"
    "Shashank Bhattarai\n"
    "ACE082BCT078\n"
    "shashankbhattarai006@gmail.com";

/**
 * @brief The copyright / academic-use notice (SPEC §10), quoted verbatim.
 *
 * UTF-8 encoded; construct with `QString::fromUtf8` or Qt 6's implicit
 * `QString(const char*)` conversion, which is already UTF-8.
 */
inline constexpr const char* kCopyrightNotice =
    "© 2026 AluChop Restaurant Management System. Developed by Shashank Bhattarai (ACE082BCT078). "
    "For academic use as an ENCT151 Object-Oriented Programming coursework project. All rights reserved.";

/**
 * @brief The 14 menu categories required by SPEC §2, in display order.
 *
 * Single source of truth: MenuService::categories(), the seed importer, the
 * menu page's filter combo and MenuItem::setCategory validation all read this
 * one array, so a category can never exist in one place and not another.
 */
/// @oop-concept Object Arrays :: the 14 required categories as one const array — single source of truth
inline const std::array<const char*, 14> kMenuCategories{
    "Sushi", "Pizza", "Pasta", "Main Course", "Dimsum", "From the Tandoor", "From the Wok",
    "Bread & Rice", "Dessert", "Drinks", "Beer", "Wine", "Mocktails", "Shots"
};

/**
 * @brief The Sage-Green design tokens of SPEC §1, verbatim.
 *
 * These are the canonical light-theme hex values; `gui::ThemeManager::kLight`
 * is built from them so the palette is stated once in the codebase and the
 * QSS generator cannot drift from the specification.
 */
/// @oop-concept Constants :: named design tokens instead of hex literals scattered through the GUI
namespace sage {
inline constexpr const char* kPrimary    = "#5D7A66"; ///< Sidebar, primary buttons, headings.
inline constexpr const char* kSecondary  = "#7E9B84"; ///< Secondary buttons, hovered chrome.
inline constexpr const char* kAccent     = "#A8C3A1"; ///< Highlights, selection, chart accent.
inline constexpr const char* kBackground = "#F6F8F4"; ///< Window background.
inline constexpr const char* kCard       = "#FFFFFF"; ///< Card and panel surfaces.
inline constexpr const char* kBorder     = "#D7E4D2"; ///< Hairlines and separators.
inline constexpr const char* kSuccess    = "#5E9E66"; ///< Paid, in-stock, confirmed.
inline constexpr const char* kDanger     = "#D16464"; ///< Cancelled, out-of-stock, destructive.
inline constexpr const char* kText       = "#1F2D1F"; ///< Primary type colour.
} // namespace sage

/**
 * @brief Currency and logging tuning constants owned by the core layer.
 *
 * Domain constants that belong to a specific class (overtime rate, minimum
 * password length, undo depth, schema version) live with that class, not here.
 */
namespace tuning {
inline constexpr std::int64_t kPaisaPerRupee   = 100;   ///< 1 NPR = 100 paisa; Money's scale.
inline constexpr int          kMoneyDecimals   = 2;     ///< Digits printed after the decimal point.
inline constexpr char         kThousandsSep    = ',';   ///< Grouping separator used by Money::toString().
inline constexpr char         kDecimalSep      = '.';   ///< Decimal separator used by Money::toString().
inline constexpr const char*  kCurrencyPrefix  = "Rs "; ///< Display prefix, e.g. "Rs 1,250.00".
inline constexpr const char*  kCurrencyCode    = "NPR"; ///< Stream/export prefix, e.g. "NPR 1250.00".
inline constexpr const char*  kDefaultLogDir   = "logs";               ///< Default log directory.
inline constexpr const char*  kDefaultLogFile  = "logs/aluchop.log";   ///< Logger's default target.
} // namespace tuning

} // namespace aluchop::core

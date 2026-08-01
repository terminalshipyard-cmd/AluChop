#pragma once

/**
 * @file SettingsPage.hpp
 * @brief Screen 9 — restaurant information, theme, database backup/restore/export.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QString>

#include "aluchop/gui/Page.hpp"

class QComboBox;
class QLineEdit;
class QListWidget;

namespace aluchop::gui {

/**
 * @brief Configuration and data safety in one place.
 *
 * Restaurant identity (name, address, phone) is stored through services::SettingsService's
 * key/value table and reused by receipts and report footers, so it is entered exactly once.
 *
 * The theme selector is the only place in the application that writes the theme preference:
 * it flips gui::ThemeManager (live, no restart) *and* persists the choice through
 * SettingsService so the next launch starts in the same mode.
 *
 * Backup, restore and export are orchestrated by services::SettingsService over the backup
 * manager down in the persistence layer — a timestamped copy of the SQLite file plus a validated
 * swap-in on restore (the file's SQLite header magic is checked before it is trusted).
 * The GUI supplies the confirmation and the toast; it never copies a file itself.
 *
 * @oop-concept Encapsulation :: even destructive operations are requests to a service, never
 *              direct file or database access from a widget
 */
class SettingsPage final : public Page {
    Q_OBJECT
public:
    /// @param ctx composition root; uses AppContext::settings().
    /// @param parent Qt parent.
    explicit SettingsPage(services::AppContext& ctx, QWidget* parent = nullptr);

    /// @return "Settings".
    QString pageTitle() const override;

    /// Reloads restaurant info, the current theme and the list of existing backups.
    void refresh() override;

private slots:
    /// Validates and saves restaurant name/address/phone.
    void onSaveInfo();

    /// Applies the chosen theme immediately and persists the preference.
    void onThemeToggled();

    /// Creates a timestamped backup of the database and refreshes the backup list.
    void onBackup();

    /// Restores the selected backup after an explicit confirmation and a validity check.
    void onRestore();

    /// Exports the whole database to the exports/ folder (CSV bundle + .db copy).
    void onExportAll();

private:
    QLineEdit* m_name = nullptr;      ///< Restaurant name (appears on receipts and reports).
    QLineEdit* m_address = nullptr;   ///< Street address.
    QLineEdit* m_phone = nullptr;     ///< Contact number.
    QComboBox* m_theme = nullptr;     ///< Light | Dark, bound to gui::ThemeManager::Mode.
    QListWidget* m_backups = nullptr; ///< Existing backups, newest first.
};

} // namespace aluchop::gui

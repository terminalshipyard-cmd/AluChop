#pragma once

/**
 * @file BackupManager.hpp
 * @brief Timestamped database backups, validated restores and export listing.
 *
 * A restore is destructive, so the candidate file is proved to be a real SQLite database first —
 * by reading its 16-byte header with a raw binary `std::ifstream`, not by trusting the extension.
 */

#include <vector>

#include <QString>

namespace aluchop::persistence {

/// @brief Copies, validates and lists `.db` backups under a backup directory.
class BackupManager {
public:
    /**
     * @param dbPath    live database file, e.g. `<dataDir>/aluchop.db`.
     * @param backupDir directory that holds `aluchop-YYYYMMDD-HHmmss.db` snapshots.
     * /// @oop-concept Parameterised Constructor :: both paths are fixed for the object's lifetime
     */
    BackupManager(QString dbPath, QString backupDir);

    /**
     * @brief Checkpoints the write-ahead log and copies the database to a timestamped file.
     * @return the absolute path of the backup just written.
     * @throws core::FileIOException when the directory cannot be created or the copy fails.
     */
    QString createBackup();

    /**
     * @brief Replaces the live database with @p backupFile.
     * @details Validates the header, closes the `Database` connection, swaps the file, reopens.
     * @throws core::FileIOException when @p backupFile is missing or not a SQLite database.
     * @throws core::DatabaseException when the restored file cannot be reopened.
     */
    void restoreBackup(const QString& backupFile);

    /// @return absolute paths of all backups, newest first.
    std::vector<QString> listBackups() const;

    /**
     * @brief Reads the first 16 bytes of @p path and compares them with `"SQLite format 3\0"`.
     * @return true when the file really is a SQLite 3 database.
     * /// @oop-concept File Handling (Binary Read + Error Checking) :: an explicitly checked
     * /// std::ifstream header read is what makes a destructive restore safe
     */
    static bool isValidSqliteFile(const QString& path);

private:
    QString m_dbPath;      ///< live database file
    QString m_backupDir;   ///< directory holding the snapshots
};

} // namespace aluchop::persistence

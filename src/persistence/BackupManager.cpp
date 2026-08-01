/**
 * @file BackupManager.cpp
 * @brief Timestamped database snapshots and validated restores (SPEC §4, docs/ARCHITECTURE.md §7).
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * A restore overwrites the live database, so nothing here trusts a file extension. The candidate is
 * proved to be a real SQLite database by reading its 16-byte header with a raw binary
 * `std::ifstream` and comparing it byte for byte with `"SQLite format 3\0"`. Only then is the live
 * connection closed, and even then the current database is snapshotted first so a failed copy can
 * be undone.
 */

#include "aluchop/persistence/BackupManager.hpp"

#include <cstring>
#include <fstream>
#include <ios>
#include <utility>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QStringList>

#include "aluchop/core/Exceptions.hpp"
#include "aluchop/persistence/Database.hpp"

namespace aluchop::persistence {
namespace {

/// The first 16 bytes of every SQLite 3 database file, trailing NUL included.
constexpr char kSqliteMagic[16] = {'S', 'Q', 'L', 'i', 't', 'e', ' ', 'f',
                                   'o', 'r', 'm', 'a', 't', ' ', '3', '\0'};

/// Snapshots are named so that a plain alphabetical sort is also a chronological sort.
const QString kBackupPattern = QStringLiteral("aluchop-*.db");

/// @brief Builds `aluchop-YYYYMMDD-HHmmss[-n].db`, avoiding a collision inside the same second.
QString uniqueBackupPath(const QDir& dir, const QString& tag)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString base = QStringLiteral("aluchop-");
    if (!tag.isEmpty()) {
        base += tag + QLatin1Char('-');
    }
    base += stamp;

    QString candidate = dir.absoluteFilePath(base + QStringLiteral(".db"));
    int suffix = 1;
    while (QFile::exists(candidate)) {
        candidate = dir.absoluteFilePath(base + QStringLiteral("-%1.db").arg(suffix));
        ++suffix;
    }
    return candidate;
}

} // namespace

BackupManager::BackupManager(QString dbPath, QString backupDir)
    : m_dbPath(std::move(dbPath)), m_backupDir(std::move(backupDir))
{
}

QString BackupManager::createBackup()
{
    if (!QFile::exists(m_dbPath)) {
        throw core::FileIOException("backup: the live database file does not exist",
                                    m_dbPath.toStdString());
    }

    QDir dir(m_backupDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        throw core::FileIOException("backup: cannot create the backup directory",
                                    QFileInfo(m_backupDir).absoluteFilePath().toStdString());
    }

    // Fold any write-ahead log back into the main file first, so the copy is self-contained.
    // Harmless (and a no-op) in the default rollback-journal mode this application runs in.
    Database& db = Database::instance();
    if (db.isOpen()) {
        try {
            db.exec(QStringLiteral("PRAGMA wal_checkpoint(FULL)"));
        } catch (const core::DatabaseException&) {
            // Not fatal: the pragma is an optimisation, not a precondition of a valid copy.
        }
    }

    const QString target = uniqueBackupPath(dir, QString());
    if (!QFile::copy(m_dbPath, target)) {
        throw core::FileIOException("backup: copying the database failed",
                                    target.toStdString());
    }
    if (!isValidSqliteFile(target)) {
        QFile::remove(target);
        throw core::FileIOException("backup: the copy is not a readable SQLite database",
                                    target.toStdString());
    }
    return target;
}

void BackupManager::restoreBackup(const QString& backupFile)
{
    if (!QFile::exists(backupFile)) {
        throw core::FileIOException("restore: the selected backup does not exist",
                                    backupFile.toStdString());
    }
    // The whole point of this class: prove the file before destroying anything.
    if (!isValidSqliteFile(backupFile)) {
        throw core::FileIOException("restore: the selected file is not a SQLite 3 database",
                                    backupFile.toStdString());
    }

    Database& db = Database::instance();
    const QString reopenPath = m_dbPath;

    // Safety net: keep the database we are about to overwrite, so a half-finished restore is
    // recoverable rather than fatal.
    QString rollbackCopy;
    if (QFile::exists(m_dbPath)) {
        QDir dir(m_backupDir);
        if (dir.exists() || dir.mkpath(QStringLiteral("."))) {
            const QString candidate = uniqueBackupPath(dir, QStringLiteral("prerestore"));
            if (QFile::copy(m_dbPath, candidate)) {
                rollbackCopy = candidate;
            }
        }
    }

    db.close(); // the file cannot be swapped while SQLite holds it open

    if (QFile::exists(m_dbPath) && !QFile::remove(m_dbPath)) {
        db.open(reopenPath); // nothing was changed yet — put the application back as it was
        throw core::FileIOException("restore: the live database file could not be replaced",
                                    m_dbPath.toStdString());
    }
    // Rollback-journal and WAL side files belong to the database we just removed.
    QFile::remove(m_dbPath + QStringLiteral("-journal"));
    QFile::remove(m_dbPath + QStringLiteral("-wal"));
    QFile::remove(m_dbPath + QStringLiteral("-shm"));

    if (!QFile::copy(backupFile, m_dbPath)) {
        if (!rollbackCopy.isEmpty()) {
            QFile::copy(rollbackCopy, m_dbPath); // undo: the user keeps the data they had
        }
        db.open(reopenPath);
        throw core::FileIOException("restore: copying the backup over the database failed",
                                    backupFile.toStdString());
    }

    db.open(reopenPath); // throws core::DatabaseException if the restored file will not open
}

std::vector<QString> BackupManager::listBackups() const
{
    std::vector<QString> out;

    QDir dir(m_backupDir);
    if (!dir.exists()) {
        return out;
    }

    const QFileInfoList entries =
        dir.entryInfoList(QStringList{kBackupPattern}, QDir::Files | QDir::Readable, QDir::Time);
    out.reserve(static_cast<std::size_t>(entries.size()));
    for (const QFileInfo& info : entries) {
        out.push_back(info.absoluteFilePath()); // QDir::Time already sorts newest first
    }
    return out;
}

bool BackupManager::isValidSqliteFile(const QString& path)
{
    if (path.trimmed().isEmpty()) {
        return false;
    }

    /// @oop-concept File Handling (Binary Read + Error Checking) :: the header is read as raw
    /// bytes and every stream state is tested; a file that cannot be opened, is too short, or
    /// carries the wrong signature is rejected before any destructive step runs.
    std::ifstream in(QFileInfo(path).absoluteFilePath().toStdString(), std::ios::in | std::ios::binary);
    if (!in.is_open() || !in.good()) {
        return false;
    }

    char header[sizeof(kSqliteMagic)] = {};
    in.read(header, static_cast<std::streamsize>(sizeof(header)));
    const bool readAll =
        in.gcount() == static_cast<std::streamsize>(sizeof(header)) && !in.bad();
    in.close();
    if (!readAll) {
        return false;
    }

    return std::memcmp(header, kSqliteMagic, sizeof(kSqliteMagic)) == 0;
}

} // namespace aluchop::persistence

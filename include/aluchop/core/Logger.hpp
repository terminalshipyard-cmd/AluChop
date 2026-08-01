#pragma once
/**
 * @file Logger.hpp
 * @brief Process-wide append-mode text logger built on raw `<fstream>`.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * This is one of the three genuinely distinct raw-file mechanisms required by
 * SPEC §5.6. It owns `logs/aluchop.log`, opened with `std::ios::app`, and only
 * ever appends whole lines of the shape:
 *
 *     [2026-08-01 12:00:00] [INFO] Order #42 settled
 *
 * The other two mechanisms live in the persistence layer:
 * `BinaryRecordFile` (fixed-record binary + random access) and
 * `CsvWriter` (sequential ASCII export).
 *
 * Every stream operation is state-checked; a failed write throws
 * FileIOException. The destructor is the sole exception to that rule — it
 * flushes and closes quietly, because throwing from a destructor during stack
 * unwinding would terminate the program.
 */

#include <fstream>

#include <QString>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::core {

/**
 * @brief Singleton line logger with severity levels and a process-wide message counter.
 *
 * Access is always through instance(); construction, copying and destruction
 * are closed off so exactly one log file handle exists per process.
 */
/// @oop-concept Static Members :: process-wide singleton logger with a static message counter
class Logger {
public:
    /**
     * @brief Severity of a log line.
     */
    /// @oop-concept Enumerations :: a closed severity vocabulary, scoped so `Level::Error` cannot collide
    enum class Level {
        Debug, ///< Developer detail; noisy.
        Info,  ///< Normal lifecycle events.
        Warn,  ///< Recoverable anomalies (low stock, failed login attempt).
        Error  ///< Failures that aborted an operation.
    };

    /**
     * @brief The one Logger in the process (Meyers singleton — constructed on first use).
     * @return Reference to the single instance.
     *
     * The instance is a function-local static, so it is thread-safe to
     * initialise and destroyed automatically at exit (RAII, §9 rule 6).
     */
    static Logger& instance();

    /**
     * @brief Points the logger at a different file, reopening in append mode.
     * @param path Path of the log file; parent directories are created if missing.
     * @throws FileIOException when the file cannot be opened for appending.
     */
    void setLogFile(const QString& path);

    /**
     * @brief Appends a line at Level::Info.
     * @param message Text to append.
     * @throws FileIOException when the write fails.
     */
    /// @oop-concept Function Overloading :: same verb, two arities — severity is optional at the call site
    void log(const QString& message);

    /**
     * @brief Appends a line at an explicit severity.
     * @param level   Severity to stamp on the line.
     * @param message Text to append.
     * @throws FileIOException when the write fails.
     */
    void log(Level level, const QString& message);

    /**
     * @brief Appends a Level::Debug line.
     * @param m Text to append.
     */
    void debug(const QString& m);

    /**
     * @brief Appends a Level::Info line.
     * @param m Text to append.
     */
    void info(const QString& m);

    /**
     * @brief Appends a Level::Warn line.
     * @param m Text to append.
     */
    void warn(const QString& m);

    /**
     * @brief Appends a Level::Error line.
     * @param m Text to append.
     */
    void error(const QString& m);

    /**
     * @brief Number of lines this process has written since start-up.
     * @return The static counter shared by every call to log().
     */
    static int messagesLogged() noexcept;

    /**
     * @brief Fixed uppercase tag written between brackets for a severity.
     * @param level Severity to name.
     * @return A static string: "DEBUG", "INFO", "WARN" or "ERROR".
     */
    static const char* levelName(Level level) noexcept {
        switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
        }
        return "INFO";
    }

    /**
     * @brief Path of the file currently being appended to.
     * @return const reference to the stored path.
     */
    const QString& logFilePath() const noexcept { return m_path; }

    /**
     * @brief Whether the underlying stream is open and writable.
     * @return true when the log file is open.
     */
    bool isOpen() const noexcept { return m_out.is_open(); }

    /** @brief Copying a singleton that owns a file handle is meaningless — deleted. */
    Logger(const Logger&) = delete;

    /** @brief Assigning a singleton that owns a file handle is meaningless — deleted. */
    Logger& operator=(const Logger&) = delete;

private:
    /**
     * @brief Opens the default log file `logs/aluchop.log` in append mode.
     *
     * Private so the singleton cannot be duplicated. Failure to open is
     * tolerated here (the app must still start); the next write reports it.
     */
    Logger();

    /**
     * @brief Flushes and closes the stream. Never throws.
     */
    /// @oop-concept Destructor :: RAII — the file handle is released deterministically at exit
    ~Logger();

    /**
     * @brief The append-mode output stream.
     */
    /// @oop-concept File Handling (Append) :: std::ofstream opened with std::ios::app, line-sequential writes
    std::ofstream m_out;

    QString m_path;            ///< Path currently open.
    static int s_messageCount; ///< Static data member; defined in `Logger.cpp`.
};

} // namespace aluchop::core

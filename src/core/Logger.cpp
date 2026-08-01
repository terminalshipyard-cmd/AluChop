/// \file
/// \brief Implementation of the process-wide append-mode text logger.
///
/// This is the *append* member of the three raw-file mechanisms demanded by
/// SPEC §5.6. It never seeks, never truncates and never rewrites: every call
/// adds one more line to the end of `logs/aluchop.log` through a
/// `std::ofstream` opened with `std::ios::app`.
///
/// Error handling is deliberately asymmetric. Opening may fail quietly (the
/// application must still start on a read-only working directory), but a failed
/// *write* throws FileIOException so the failure is not silently lost — except
/// in the destructor, where throwing would abort the process during shutdown.
///
/// \author Shashank Bhattarai (ACE082BCT078)

#include "aluchop/core/Logger.hpp"

#include <ios>
#include <string>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QString>

#include "aluchop/core/Exceptions.hpp"

namespace aluchop::core {

/// @oop-concept Static Members :: the counter is shared by every Logger call in the process
int Logger::s_messageCount = 0;

namespace {

/// \brief Default log file, relative to the working directory.
const char* const kDefaultLogPath = "logs/aluchop.log";

/// \brief Ensures the directory that will hold \p filePath exists.
/// \param filePath Full path of the intended log file.
/// \return true when the parent directory exists (or was created).
bool ensureParentDirectory(const QString& filePath)
{
    const QFileInfo info(filePath);
    const QString dirPath = info.absolutePath();
    if (dirPath.isEmpty())
        return true;
    QDir dir;
    return dir.exists(dirPath) || dir.mkpath(dirPath);
}

/// \brief Opens \p stream on \p filePath in append mode.
/// \param stream Stream to (re)open; closed first if already open.
/// \param filePath Target file.
/// \return true when the stream is open and writable afterwards.
bool openForAppend(std::ofstream& stream, const QString& filePath)
{
    if (stream.is_open())
        stream.close();
    stream.clear();
    if (!ensureParentDirectory(filePath))
        return false;
    /// @oop-concept File Handling (Append) :: std::ios::app — writes always land at end of file
    stream.open(filePath.toStdString(), std::ios::out | std::ios::app);
    return stream.is_open() && stream.good();
}

} // namespace

Logger::Logger()
{
    // Preferred location first; a temp-directory fallback keeps the application
    // usable when the working directory is not writable. Neither failure is
    // fatal here — the next write reports the problem.
    if (openForAppend(m_out, QString::fromLatin1(kDefaultLogPath))) {
        m_path = QString::fromLatin1(kDefaultLogPath);
        return;
    }

    const QString fallback = QDir(QDir::tempPath()).filePath(QStringLiteral("aluchop.log"));
    if (openForAppend(m_out, fallback))
        m_path = fallback;
    else
        m_path = QString::fromLatin1(kDefaultLogPath);
}

/// @oop-concept Destructor :: RAII — the handle is flushed and released deterministically
Logger::~Logger()
{
    // Never throws: a destructor that throws during stack unwinding terminates
    // the program, so shutdown failures are swallowed on purpose.
    if (m_out.is_open()) {
        m_out.flush();
        m_out.close();
    }
}

Logger& Logger::instance()
{
    // Meyers singleton: initialised on first use, destroyed at exit, and
    // thread-safe to initialise under C++11 and later.
    static Logger theLogger;
    return theLogger;
}

void Logger::setLogFile(const QString& path)
{
    const QString target = path.trimmed();
    if (target.isEmpty())
        throw FileIOException("log file path must not be empty");

    if (!openForAppend(m_out, target))
        throw FileIOException("cannot open log file for appending: " + target.toStdString());

    m_path = target;
}

/// @oop-concept Function Overloading :: the one-argument form defaults the severity to Info
void Logger::log(const QString& message)
{
    log(Level::Info, message);
}

void Logger::log(Level level, const QString& message)
{
    if (!m_out.is_open())
        throw FileIOException("log file is not open: " + m_path.toStdString());

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString line = QStringLiteral("[%1] [%2] %3")
                             .arg(stamp,
                                  QString::fromLatin1(levelName(level)),
                                  QString(message).replace(QLatin1Char('\n'), QLatin1Char(' ')));

    const QByteArray utf8 = line.toUtf8();
    m_out.write(utf8.constData(), static_cast<std::streamsize>(utf8.size()));
    m_out.put('\n');
    m_out.flush();

    // Sequential-append error checking, as required by SPEC §5.6.
    if (!m_out.good()) {
        m_out.clear();
        throw FileIOException("failed writing to log file: " + m_path.toStdString());
    }

    ++s_messageCount;
}

void Logger::debug(const QString& m)
{
    log(Level::Debug, m);
}

void Logger::info(const QString& m)
{
    log(Level::Info, m);
}

void Logger::warn(const QString& m)
{
    log(Level::Warn, m);
}

void Logger::error(const QString& m)
{
    log(Level::Error, m);
}

int Logger::messagesLogged() noexcept
{
    return s_messageCount;
}

} // namespace aluchop::core

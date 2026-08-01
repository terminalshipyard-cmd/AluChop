/**
 * @file main.cpp
 * @brief Application entry point: theme → splash → composition root → login → shell.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The whole start-up sequence, and the only place in the application that owns a top-level
 * lifetime decision:
 *
 *  1. Build the QApplication and apply the Sage-Green theme before anything is shown, so no
 *     window ever flashes in Qt's default styling.
 *  2. Put the branded splash on screen and fade it in.
 *  3. Construct services::AppContext — this opens SQLite, runs the migrations, seeds the menu on
 *     first run, opens the binary audit file and rebuilds the kitchen queue. Every way that can
 *     fail is caught **separately** here, and each failure gets its own honest explanation in a
 *     dialog rather than a silent exit.
 *  4. Apply the persisted theme preference.
 *  5. Honour a remembered login, otherwise show gui::LoginWindow.
 *  6. On successful authentication, build gui::MainWindow and hand over.
 *
 * @par Lifetime
 * `ctx` is a std::unique_ptr living in runSession()'s frame, and runSession() is the function
 * that calls `app.exec()` — so the composition root is guaranteed to outlive every window that
 * holds a reference to it (ARCHITECTURE §9 rule 4). Windows are heap-allocated with no parent
 * and Qt::WA_DeleteOnClose (rule 2).
 *
 * @oop-concept Multiple Catch :: five catch clauses over one try — each subtype of
 *              core::AluChopException means a genuinely different failure with a different
 *              message, and std::exception plus a catch-all close the door on the rest
 * @oop-concept Dynamic Memory Allocation :: windows are created with `new`; Qt's WA_DeleteOnClose
 *              and parent ownership retire them, so there is not one manual `delete` in the file
 */

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QMessageBox>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include <exception>
#include <functional>
#include <memory>
#include <optional>

#include "aluchop/core/AppInfo.hpp"
#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Logger.hpp"
#include "aluchop/gui/LoginWindow.hpp"
#include "aluchop/gui/MainWindow.hpp"
#include "aluchop/gui/SplashScreen.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/models/User.hpp"
#include "aluchop/services/AppContext.hpp"
#include "aluchop/services/AuthService.hpp"
#include "aluchop/services/SettingsService.hpp"

namespace {

using aluchop::core::kAppInfo;

/// Shows a modal, non-technical failure dialog. Used only for start-up faults, where there is no
/// main window yet to raise a toast on.
/// @param headline one short sentence the user can act on.
/// @param detail the underlying message, shown as secondary text.
void showFatal(const QString& headline, const QString& detail) {
    QMessageBox box;
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle(QStringLiteral("AluChop — cannot start"));
    box.setText(headline);
    box.setInformativeText(detail);
    box.setDetailedText(
        QStringLiteral("%1 v%2\n%3 (%4)\n%5")
            .arg(QString::fromUtf8(kAppInfo.appName), QString::fromUtf8(kAppInfo.version),
                 QString::fromUtf8(kAppInfo.developer), QString::fromUtf8(kAppInfo.rollNo),
                 QString::fromUtf8(kAppInfo.email)));
    box.setStandardButtons(QMessageBox::Close);
    box.exec();
}

/// Writes one line to the application log, swallowing any I/O failure.
///
/// core::Logger throws core::FileIOException when its stream goes bad. That is the right
/// behaviour for a logger, but it must never be what the user sees: an unwritable log file is not
/// a reason to abort a start-up, and it must certainly not replace a precise database error
/// message with a confusing file-system one.
void logSafely(aluchop::core::Logger::Level level, const QString& message) {
    try {
        aluchop::core::Logger::instance().log(level, message);
    } catch (...) {
        // Deliberately ignored — see above.
    }
}

/// @return the writable folder that holds `aluchop.db`, `audit.bin`, backups and logs.
///
/// QStandardPaths::AppDataLocation already ends in the application name on every platform Qt
/// supports, so the ARCHITECTURE §3.5 "+ /AluChop" suffix is added only when it is missing.
QString resolveDataDir() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.aluchop");
    }
    if (!dir.endsWith(QStringLiteral("/AluChop"))) {
        dir += QStringLiteral("/AluChop");
    }
    return QDir::cleanPath(dir);
}

/// @return the first existing candidate for `assets/menu/menu_seed.json`.
///
/// The binary is run both straight out of `build/` (where CMake stages a copy of `assets/`) and
/// from the project root, so all the plausible locations are tried. When none exists the first
/// candidate is returned anyway: AppContext then fails with a precise, catchable error instead of
/// this function inventing one.
QString resolveMenuSeedPath() {
    const QString relative = QStringLiteral("assets/menu/menu_seed.json");
    const QStringList candidates{
        QCoreApplication::applicationDirPath() + QLatin1Char('/') + relative,
        QDir::currentPath() + QLatin1Char('/') + relative,
        QCoreApplication::applicationDirPath() + QStringLiteral("/../") + relative,
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../") + relative,
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return QDir::cleanPath(candidates.first());
}

/// Keeps the event loop breathing for @p ms so an animation can actually be seen before the
/// next blocking step. The application is single-threaded by contract (ARCHITECTURE §9), so this
/// is how start-up stays visually alive while the database opens.
void pumpFor(int ms) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

/**
 * @brief The real body of the program.
 *
 * Split out of main() so that the composition root, the window pointers and the session flag all
 * live in one frame that provably outlives `app.exec()`.
 *
 * @return the process exit code.
 */
int runSession(QApplication& app) {
    using namespace aluchop;

    // --- 1. writable storage --------------------------------------------------------------
    const QString dataDir = resolveDataDir();
    QDir().mkpath(dataDir);
    QDir().mkpath(dataDir + QStringLiteral("/logs"));

    // A missing log file must never stop a restaurant from opening, so this one failure mode is
    // handled locally and downgraded to a warning.
    try {
        core::Logger::instance().setLogFile(dataDir + QStringLiteral("/logs/aluchop.log"));
        logSafely(core::Logger::Level::Info,
                  QStringLiteral("AluChop %1 starting — data dir %2")
                      .arg(QString::fromUtf8(kAppInfo.version), dataDir));
    } catch (const core::FileIOException& e) {
        qWarning("AluChop: file logging unavailable (%s)", e.what());
    }

    // --- 2. splash ---------------------------------------------------------------------------
    gui::SplashScreen splash;
    bool authenticated = false;

    // The continuation runs once the splash has faded out; it is defined before showFor() is
    // called, but only invoked from inside the event loop, by which time `ctx` is populated.
    std::unique_ptr<services::AppContext> ctx;
    std::function<void()> openShell;
    std::function<void()> afterSplash;

    openShell = [&app, &ctx, &authenticated]() {
        authenticated = true;
        auto* window = new gui::MainWindow(*ctx);
        window->setAttribute(Qt::WA_DeleteOnClose);
        // Closing the shell ends the session — the application deliberately does not quit on the
        // last window, because the splash-to-login and login-to-shell handovers both pass through
        // a moment with no window on screen.
        QObject::connect(window, &QObject::destroyed, &app, []() { QCoreApplication::quit(); });
        window->show();
        window->raise();
        window->activateWindow();
    };

    afterSplash = [&app, &ctx, &authenticated, &openShell]() {
        // Remember-me: a stored token signs the user straight in and the login window is skipped.
        if (ctx->auth().tryRememberedLogin().has_value()) {
            logSafely(core::Logger::Level::Info,
                      QStringLiteral("Signed in from a remembered session"));
            openShell();
            return;
        }

        auto* login = new gui::LoginWindow(*ctx);
        login->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(login, &gui::LoginWindow::loggedIn, login, [&openShell, login]() {
            openShell();      // build the shell first…
            login->close();   // …so the session never passes through zero windows
        });
        QObject::connect(login, &QObject::destroyed, &app, [&authenticated]() {
            if (!authenticated) {
                QCoreApplication::quit();   // the user closed the sign-in window
            }
        });
        login->show();
        login->raise();
        login->activateWindow();
    };

    splash.showFor(1200, afterSplash);
    splash.showMessage(QStringLiteral("Starting up…"), Qt::AlignBottom | Qt::AlignHCenter,
                       Qt::white);
    pumpFor(380);   // let the fade-in play before the database work blocks the loop

    // --- 3. the composition root ---------------------------------------------------------------
    const QString seedPath = resolveMenuSeedPath();
    splash.showMessage(QStringLiteral("Opening the database…"),
                       Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
    QCoreApplication::processEvents();

    try {
        ctx = std::make_unique<services::AppContext>(dataDir, seedPath);
    }
    /// @oop-concept Multiple Catch :: four different faults, four different explanations
    catch (const core::DatabaseException& e) {
        splash.close();
        logSafely(core::Logger::Level::Error, QString::fromUtf8(e.what()));
        showFatal(QStringLiteral("The AluChop database could not be opened."),
                  QStringLiteral("%1\n\nDatabase folder:\n%2").arg(QString::fromUtf8(e.what()),
                                                                   dataDir));
        return 1;
    } catch (const core::FileIOException& e) {
        splash.close();
        logSafely(core::Logger::Level::Error, QString::fromUtf8(e.what()));
        showFatal(QStringLiteral("A data file could not be opened for writing."),
                  QStringLiteral("%1\n\nCheck that this folder exists and is writable:\n%2")
                      .arg(QString::fromUtf8(e.what()), dataDir));
        return 1;
    } catch (const core::ValidationException& e) {
        splash.close();
        logSafely(core::Logger::Level::Error, QString::fromUtf8(e.what()));
        showFatal(QStringLiteral("The menu seed file is missing or malformed."),
                  QStringLiteral("%1\n\nExpected seed file:\n%2")
                      .arg(QString::fromUtf8(e.what()), seedPath));
        return 1;
    } catch (const core::AluChopException& e) {
        splash.close();
        logSafely(core::Logger::Level::Error, QString::fromUtf8(e.what()));
        showFatal(QStringLiteral("AluChop could not finish starting up."),
                  QString::fromUtf8(e.what()));
        return 1;
    }

    // --- 4. persisted preferences ----------------------------------------------------------------
    const QString themeMode =
        ctx->settings().get(QStringLiteral("theme.mode"), QStringLiteral("light"));
    gui::ThemeManager::instance().setMode(themeMode.compare(QStringLiteral("dark"),
                                                            Qt::CaseInsensitive) == 0
                                              ? gui::ThemeManager::Mode::Dark
                                              : gui::ThemeManager::Mode::Light);

    splash.showMessage(QStringLiteral("Ready."), Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
    logSafely(core::Logger::Level::Info,
              QStringLiteral("Composition root ready — entering event loop"));

    // The splash's own timer now fires as soon as the loop resumes, calling afterSplash().
    const int code = app.exec();
    logSafely(core::Logger::Level::Info,
              QStringLiteral("AluChop exited with code %1").arg(code));
    return code;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("AluChop"));
    QCoreApplication::setOrganizationName(QStringLiteral("AluChop"));
    QCoreApplication::setApplicationVersion(QString::fromUtf8(kAppInfo.version));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("AluChop"));

    // The handover from splash to login, and from login to shell, each pass through an instant
    // with no visible window; quitting on the last closed window would end the session there.
    QApplication::setQuitOnLastWindowClosed(false);

    // Theme before any window exists, so nothing is ever painted in Qt's stock styling.
    aluchop::gui::ThemeManager::instance().apply(app);

    // Outer safety net. runSession() already gives each start-up fault its own dialog; these
    // clauses catch anything that escapes a slot, a constructor or the event loop itself, so the
    // application always dies explaining itself rather than vanishing.
    try {
        return runSession(app);
    } catch (const aluchop::core::DatabaseException& e) {
        showFatal(QStringLiteral("A database operation failed and could not be recovered."),
                  QString::fromUtf8(e.what()));
        return 1;
    } catch (const aluchop::core::AluChopException& e) {
        showFatal(QStringLiteral("AluChop stopped because of an unrecoverable error."),
                  QString::fromUtf8(e.what()));
        return 1;
    } catch (const std::exception& e) {
        showFatal(QStringLiteral("AluChop stopped because of an unexpected error."),
                  QString::fromUtf8(e.what()));
        return 1;
    } catch (...) {
        showFatal(QStringLiteral("AluChop stopped because of an unknown error."),
                  QStringLiteral("No further information is available."));
        return 1;
    }
}

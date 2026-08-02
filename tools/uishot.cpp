/**
 * @file uishot.cpp
 * @brief Offscreen screen-shooter: makes the application render every screen to a PNG.
 *
 * macOS refuses `screencapture` without a Screen Recording TCC grant, so the only reliable way
 * to actually SEE this UI is to make the app grab its own widgets with QWidget::grab().
 *
 * This is also a hard crash test: every page must construct, lay out and paint against the real
 * seeded SQLite database, with a populated order in the POS and a real prepared bill.
 *
 * Build:
 *   cmake --build build-uishot --target aluchop_uishot && ./build-uishot/aluchop_uishot
 *
 * Environment overrides (all optional):
 *   ALUCHOP_DATA_DIR   — data directory to open instead of the real one
 *   ALUCHOP_SHOT_DIR   — output directory instead of docs/screenshots
 */

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QLineEdit>
#include <QMetaObject>
#include <QPixmap>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTextStream>
#include <QWidget>

#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <vector>

#include "aluchop/core/AppInfo.hpp"
#include "aluchop/core/Exceptions.hpp"
#include "aluchop/core/Money.hpp"
#include "aluchop/gui/BillingDialog.hpp"
#include "aluchop/gui/CommandPalette.hpp"
#include "aluchop/gui/LoginWindow.hpp"
#include "aluchop/gui/MainWindow.hpp"
#include "aluchop/gui/SplashScreen.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/models/Enums.hpp"
#include "aluchop/models/MenuItem.hpp"
#include "aluchop/models/Order.hpp"
#include "aluchop/models/Reservation.hpp"
#include "aluchop/models/Table.hpp"
#include "aluchop/services/AppContext.hpp"
#include "aluchop/services/AuthService.hpp"
#include "aluchop/services/BillingService.hpp"
#include "aluchop/services/MenuService.hpp"
#include "aluchop/services/OrderService.hpp"
#include "aluchop/services/ReservationService.hpp"

namespace {

using namespace aluchop;

constexpr int kShotWidth = 1600;
constexpr int kShotHeight = 1000;

QString g_outDir;
int g_pass = 0;
int g_fail = 0;
QStringList g_failures;

void say(const QString& line) {
    QTextStream out(stdout);
    out << line << '\n';
    out.flush();
}

/// Keeps the event loop breathing so layouts settle and QPropertyAnimations finish.
void pump(int ms) {
    QElapsedTimer clock;
    clock.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
    } while (clock.elapsed() < ms);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
}

struct ImageVerdict {
    bool ok = false;
    int uniqueColours = 0;
    double dominantShare = 1.0;
    QString reason;
};

/// A grab is only useful if it is genuinely a rendered screen: many distinct colours and no single
/// colour swallowing the frame. A blank or all-white grab must be reported, never accepted.
ImageVerdict judge(const QImage& img) {
    ImageVerdict v;
    if (img.isNull() || img.width() < 32 || img.height() < 32) {
        v.reason = QStringLiteral("image is null or absurdly small");
        return v;
    }
    const QImage rgb = img.convertToFormat(QImage::Format_RGB32);
    QHash<QRgb, int> histogram;
    int sampled = 0;
    for (int y = 0; y < rgb.height(); y += 3) {
        for (int x = 0; x < rgb.width(); x += 3) {
            ++histogram[rgb.pixel(x, y)];
            ++sampled;
        }
    }
    int dominant = 0;
    for (auto it = histogram.cbegin(); it != histogram.cend(); ++it)
        dominant = qMax(dominant, it.value());

    v.uniqueColours = histogram.size();
    v.dominantShare = sampled > 0 ? static_cast<double>(dominant) / sampled : 1.0;

    if (v.uniqueColours < 12) {
        v.reason = QStringLiteral("only %1 distinct colours — looks blank").arg(v.uniqueColours);
        return v;
    }
    if (v.dominantShare > 0.985) {
        v.reason = QStringLiteral("%1%% of pixels are one colour — looks blank")
                       .arg(v.dominantShare * 100.0, 0, 'f', 1);
        return v;
    }
    v.ok = true;
    return v;
}

/// Grabs @p w, writes it, verifies the result and prints one PASS/FAIL line.
bool shoot(QWidget* w, const QString& fileName, const QString& label) {
    const QString path = g_outDir + QLatin1Char('/') + fileName;
    if (!w) {
        ++g_fail;
        g_failures << fileName;
        say(QStringLiteral("FAIL  %1  — %2: widget is null").arg(fileName, label));
        return false;
    }
    pump(120);
    const QPixmap pm = w->grab();
    const QImage img = pm.toImage();
    const bool written = img.save(path, "PNG");
    const qint64 bytes = QFileInfo(path).size();
    const ImageVerdict v = judge(img);

    const bool ok = written && bytes > 4096 && v.ok;
    if (ok) {
        ++g_pass;
        say(QStringLiteral("PASS  %1  — %2  (%3x%4, %5 colours, %6 KB)")
                .arg(fileName, label)
                .arg(img.width())
                .arg(img.height())
                .arg(v.uniqueColours)
                .arg(bytes / 1024));
    } else {
        ++g_fail;
        g_failures << fileName;
        QString why = v.ok ? QString() : v.reason;
        if (!written) why = QStringLiteral("PNG could not be written");
        else if (bytes <= 4096) why = QStringLiteral("file is only %1 bytes").arg(bytes);
        say(QStringLiteral("FAIL  %1  — %2: %3").arg(fileName, label, why));
    }
    return ok;
}

QString resolveDataDir() {
    const QByteArray override = qgetenv("ALUCHOP_DATA_DIR");
    if (!override.isEmpty()) return QDir::cleanPath(QString::fromLocal8Bit(override));

    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + QStringLiteral("/.aluchop");
    if (!dir.endsWith(QStringLiteral("/AluChop"))) dir += QStringLiteral("/AluChop");
    return QDir::cleanPath(dir);
}

QString resolveMenuSeedPath() {
    const QString relative = QStringLiteral("assets/menu/menu_seed.json");
    const QStringList candidates{
        QCoreApplication::applicationDirPath() + QLatin1Char('/') + relative,
        QDir::currentPath() + QLatin1Char('/') + relative,
        QCoreApplication::applicationDirPath() + QStringLiteral("/../") + relative,
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../") + relative,
    };
    for (const QString& c : candidates)
        if (QFileInfo::exists(c)) return QDir::cleanPath(c);
    return QDir::cleanPath(candidates.first());
}

/**
 * @brief Finds the OrdersPage's "Active Orders" table (5 columns) and selects the row that shows
 *        @p orderId, so the POS screenshot shows the ticket this tool created through OrderService.
 *
 * Falls back to the first row only when the id cannot be matched, and says so.
 */
void selectOrderRow(QWidget* shell, const QString& orderNumber) {
    const auto tables = shell->findChildren<QTableWidget*>();
    for (QTableWidget* t : tables) {
        if (t->columnCount() != 5 || t->rowCount() == 0 || !t->isVisible()) continue;
        int target = -1;
        for (int row = 0; row < t->rowCount() && target < 0; ++row) {
            for (int col = 0; col < t->columnCount(); ++col) {
                const auto* cell = t->item(row, col);
                if (cell && !orderNumber.isEmpty() && cell->text() == orderNumber) {
                    target = row;
                    break;
                }
            }
        }
        if (target < 0) {
            target = 0;
            say(QStringLiteral("WARN  order %1 not found in the active-orders table — "
                               "selecting row 0 instead")
                    .arg(orderNumber));
        }
        t->setCurrentCell(target, 0);
        t->selectRow(target);
        return;
    }
    say(QStringLiteral("WARN  no visible active-orders table found to select a ticket in"));
}

/**
 * @brief Selects the first row of the visible table with @p columns columns.
 *
 * Several screens keep a detail panel to the right that stays in its "select somebody" state until
 * a row is chosen. A screenshot of a permanently unselected master list under-sells the screen, so
 * the tool makes the same first click a user would.
 */
void selectFirstRow(QWidget* shell, int columns, const QString& what) {
    for (QTableWidget* t : shell->findChildren<QTableWidget*>()) {
        if (t->columnCount() != columns || t->rowCount() == 0 || !t->isVisible()) continue;
        t->setCurrentCell(0, 0);
        t->selectRow(0);
        return;
    }
    say(QStringLiteral("WARN  no visible %1 table to select a row in").arg(what));
}

struct SeededState {
    int posOrderId = 0;    ///< Open order with several lines — the POS screenshot
    QString posOrderNumber;   ///< Its human-readable number, to select it in the orders table
    int billOrderId = 0;   ///< Served order — the BillingDialog screenshot
    int paidOrderId = 0;   ///< Settled order — gives the dashboard/reports real revenue
    int onThePass = 0;     ///< Tickets left Pending/Preparing/Ready — the Kitchen Pass
    int bookings = 0;      ///< Live bookings on today's sheet — the Reservations screen
};

/**
 * @brief Books tables for tonight through ReservationService, exactly as the booking form does.
 *
 * The Reservations screen opens on *today*, and the service refuses a slot in the past, so the
 * sittings are laid out forward from the current instant. If the tool is run so late that no
 * sitting still lands today, nothing is booked and the screen honestly shows its empty state —
 * inventing a booking for a time that has already gone would be a lie about the availability rule.
 */
int seedBookings(services::AppContext& ctx) {
    struct Guest {
        const char* name;
        const char* phone;
        int party;
        const char* request;
    };
    static const Guest guests[] = {
        {"Pratima Joshi", "+977-9841000013", 2, "Window seat, away from the bar"},
        {"Rojina Maharjan", "+977-9841000011", 4, "Birthday cake at the end"},
        {"Manish Basnet", "+977-9841000015", 6, ""},
        {"Sujan Adhikari", "+977-9841000012", 2, "Vegetarian only, no onion"},
    };

    const QDate today = QDate::currentDate();
    QDateTime slot = QDateTime::currentDateTime().addSecs(20 * 60);
    int booked = 0;
    int firstId = 0;

    // The same guest cannot be sitting at two tables at once. An earlier run of this tool could
    // leave exactly that behind, so before anything is booked the sheet is tidied the way a host
    // would: the duplicate is cancelled through the service, never deleted behind its back.
    {
        QStringList seen;
        for (const auto& booking : ctx.reservations().onDay(today)) {
            const bool live = booking.status() == models::ReservationStatus::Booked
                              || booking.status() == models::ReservationStatus::Seated;
            if (!live) continue;
            if (!seen.contains(booking.customerName())) {
                seen << booking.customerName();
                continue;
            }
            const auto dropped = ctx.reservations().cancel(booking.id());
            say(dropped.isOk()
                    ? QStringLiteral("INFO  cancelled duplicate booking %1 for %2")
                          .arg(booking.id())
                          .arg(booking.customerName())
                    : QStringLiteral("WARN  could not cancel duplicate booking %1: %2")
                          .arg(booking.id())
                          .arg(dropped.error()));
        }
    }

    // A guest already on today's sheet is left alone: re-running the tool must photograph the book
    // as it stands, not stack a second sitting on top of it.
    const auto sheet = ctx.reservations().onDay(today);
    for (const auto& booking : sheet)
        if (booking.status() == models::ReservationStatus::Booked
            || booking.status() == models::ReservationStatus::Seated)
            ++booked;

    for (const Guest& g : guests) {
        if (slot.date() != today) break;
        const QString name = QString::fromUtf8(g.name);
        bool already = false;
        for (const auto& existing : sheet)
            if (existing.customerName() == name) already = true;
        if (already) continue;

        const auto free = ctx.reservations().availableTables(slot, 90, g.party);
        if (free.empty()) {
            slot = slot.addSecs(40 * 60);
            continue;
        }
        try {
            models::Reservation booking(0, free.front().id(), name, QString::fromUtf8(g.phone),
                                        slot, 90, g.party);
            if (*g.request != '\0')
                booking.setSpecialRequest(QString::fromUtf8(g.request));
            const auto made = ctx.reservations().book(booking);
            if (made.isOk()) {
                ++booked;
                if (firstId == 0) firstId = made.value();
            } else {
                say(QStringLiteral("WARN  book(%1) failed: %2").arg(name, made.error()));
            }
        } catch (const std::exception& e) {
            say(QStringLiteral("WARN  booking for %1 was rejected: %2")
                    .arg(name, QString::fromUtf8(e.what())));
        }
        slot = slot.addSecs(30 * 60);
    }

    // One party has already walked in, so the sheet shows more than a single status.
    if (firstId > 0) {
        const auto seated = ctx.reservations().seat(firstId);
        if (!seated.isOk())
            say(QStringLiteral("WARN  seat(%1) failed: %2").arg(firstId).arg(seated.error()));
    }

    if (booked == 0)
        say(QStringLiteral("WARN  no booking could be placed for today — the Reservations "
                           "screenshot will show its empty state"));
    return booked;
}

/// Puts real, populated data on screen instead of empty tables: an empty screen proves nothing.
SeededState seedLiveState(services::AppContext& ctx) {
    SeededState s;
    const auto dishes = ctx.menu().all();
    if (dishes.empty()) {
        say(QStringLiteral("WARN  menu is empty — the POS screenshot will be bare"));
        return s;
    }

    auto openOrder = [&ctx](models::OrderType type) -> int {
        QString lastError;
        if (type == models::OrderType::DineIn) {
            for (int table = 1; table <= 12; ++table) {
                auto r = ctx.orders().createOrder(type, table, 1, 1);
                if (r.isOk()) return r.value().id();
                lastError = r.error();
            }
        } else {
            auto r = ctx.orders().createOrder(type, 0, 1, 1);
            if (r.isOk()) return r.value().id();
            lastError = r.error();
        }
        say(QStringLiteral("WARN  createOrder(%1) failed: %2")
                .arg(models::toString(type), lastError));
        return 0;
    };

    auto stock = [&ctx, &dishes](int orderId, std::size_t first, std::size_t count) {
        for (std::size_t i = 0; i < count && (first + i) < dishes.size(); ++i) {
            const auto& dish = dishes[first + i];
            if (!dish.isAvailable()) continue;
            const auto r = ctx.orders().addItem(orderId, dish.id(), 1 + static_cast<int>(i % 3));
            if (!r.isOk())
                say(QStringLiteral("WARN  addItem(%1) failed: %2").arg(dish.name(), r.error()));
        }
    };

    auto serve = [&ctx](int orderId) {
        auto sub = ctx.orders().submitToKitchen(orderId);
        if (!sub.isOk()) {
            say(QStringLiteral("WARN  submitToKitchen failed: %1").arg(sub.error()));
            return false;
        }
        for (int step = 0; step < 3; ++step) {
            auto adv = ctx.orders().advanceStatus(orderId);
            if (!adv.isOk()) {
                say(QStringLiteral("WARN  advanceStatus failed: %1").arg(adv.error()));
                return false;
            }
        }
        return true;
    };

    // 1. The live ticket the POS screen shows.
    s.posOrderId = openOrder(models::OrderType::DineIn);
    if (s.posOrderId > 0) {
        stock(s.posOrderId, 0, 6);
        if (const auto placed = ctx.orders().order(s.posOrderId))
            s.posOrderNumber = placed->orderNumber();
    }

    // 2. A served order to open the billing dialog on.
    s.billOrderId = openOrder(models::OrderType::Takeaway);
    if (s.billOrderId > 0) {
        stock(s.billOrderId, 12, 4);
        if (!serve(s.billOrderId)) s.billOrderId = 0;
    }

    // 3. A settled order, so the dashboard and reports have money to draw.
    s.paidOrderId = openOrder(models::OrderType::Delivery);
    if (s.paidOrderId > 0) {
        stock(s.paidOrderId, 24, 3);
        if (serve(s.paidOrderId)) {
            auto prepared = ctx.billing().prepareBill(s.paidOrderId, QString(), 10);
            if (prepared.isOk()) {
                auto bill = prepared.take();
                const core::Money tendered =
                    core::Money::fromRupees(bill.total().wholeRupees() + 200);
                const int cashier =
                    ctx.auth().currentUser() ? ctx.auth().currentUser()->id() : 1;
                auto paid = ctx.billing().settle(s.paidOrderId, bill,
                                                 models::PaymentMethod::Cash, tendered, cashier);
                if (!paid.isOk())
                    say(QStringLiteral("WARN  settle failed: %1").arg(paid.error()));
            } else {
                say(QStringLiteral("WARN  prepareBill failed: %1").arg(prepared.error()));
            }
        }
    }

    // 4. Three tickets LEFT ON THE PASS — one in each lane — so the Kitchen Pass photographs a
    //    working service rather than an empty board. Each is fired with submitToKitchen() and then
    //    walked forward with advanceStatus(); nothing is written behind the service's back.
    struct Lane {
        models::OrderType type;
        std::size_t firstDish;
        int advances;      ///< 0 = Pending, 1 = Preparing, 2 = Ready
        const char* lane;
    };
    static const Lane lanes[] = {
        {models::OrderType::DineIn, 36, 0, "Pending"},
        {models::OrderType::DineIn, 44, 1, "Preparing"},
        {models::OrderType::Takeaway, 52, 2, "Ready"},
    };
    for (const Lane& lane : lanes) {
        const auto status = static_cast<models::OrderStatus>(
            static_cast<int>(models::OrderStatus::Pending) + lane.advances);
        // Re-running the tool must not stack a fresh ticket into a lane that already holds one, or
        // the pass grows by three on every run. A lane that is already occupied keeps its ticket;
        // anything behind it is finished off through advanceStatus(), exactly as the kitchen would
        // clear a backlog, so the pass shows one live ticket per lane instead of a growing pile.
        const auto standing = ctx.orders().withStatus(status);
        if (!standing.empty()) {
            ++s.onThePass;
            for (std::size_t extra = 1; extra < standing.size(); ++extra) {
                for (int step = lane.advances; step < 3; ++step) {
                    const auto adv = ctx.orders().advanceStatus(standing[extra].id());
                    if (!adv.isOk()) {
                        say(QStringLiteral("WARN  could not clear backlog ticket %1: %2")
                                .arg(standing[extra].orderNumber(), adv.error()));
                        break;
                    }
                }
            }
            continue;
        }
        const int ticket = openOrder(lane.type);
        if (ticket == 0) continue;
        stock(ticket, lane.firstDish, 3);
        const auto fired = ctx.orders().submitToKitchen(ticket);
        if (!fired.isOk()) {
            say(QStringLiteral("WARN  submitToKitchen(%1) failed: %2")
                    .arg(ticket)
                    .arg(fired.error()));
            continue;
        }
        bool walked = true;
        for (int step = 0; step < lane.advances && walked; ++step) {
            const auto adv = ctx.orders().advanceStatus(ticket);
            if (!adv.isOk()) {
                say(QStringLiteral("WARN  advanceStatus(%1) to %2 failed: %3")
                        .arg(ticket)
                        .arg(QLatin1String(lane.lane), adv.error()));
                walked = false;
            }
        }
        if (walked) ++s.onThePass;
    }
    if (s.onThePass < 3)
        say(QStringLiteral("DEFECT  only %1 of 3 kitchen tickets could be left on the pass")
                .arg(s.onThePass));

    // 5. Tonight's bookings, so the Reservations sheet is a working book.
    s.bookings = seedBookings(ctx);

    // Every order below was created THROUGH OrderService. There is deliberately no fallback to
    // rows already sitting in the database: if the application cannot create an order, the POS and
    // billing screenshots must fail loudly rather than quietly photograph someone else's data.
    if (s.posOrderId == 0)
        say(QStringLiteral("DEFECT  OrderService could not create the POS order — the POS "
                           "screenshot will not show an app-created ticket"));
    if (s.billOrderId == 0)
        say(QStringLiteral("DEFECT  OrderService could not produce a SERVED order — the billing "
                           "dialog cannot be shot"));

    say(QStringLiteral("INFO  seeded live state — POS order %1, billable order %2, paid order %3, "
                       "%4 tickets on the pass, %5 bookings tonight")
            .arg(s.posOrderId)
            .arg(s.billOrderId)
            .arg(s.paidOrderId)
            .arg(s.onThePass)
            .arg(s.bookings));
    return s;
}

int run(QApplication& app) {
    // --- output directory -------------------------------------------------------------------
    const QByteArray outOverride = qgetenv("ALUCHOP_SHOT_DIR");
    g_outDir = outOverride.isEmpty()
                   ? QDir::currentPath() + QStringLiteral("/docs/screenshots")
                   : QString::fromLocal8Bit(outOverride);
    QDir().mkpath(g_outDir);
    say(QStringLiteral("INFO  writing PNGs to %1").arg(g_outDir));
    say(QStringLiteral("INFO  platform plugin: %1").arg(QGuiApplication::platformName()));

    gui::ThemeManager::instance().setMode(gui::ThemeManager::Mode::Light);
    gui::ThemeManager::instance().apply(app);

    // --- 01 splash --------------------------------------------------------------------------
    {
        gui::SplashScreen splash;
        splash.show();
        splash.showMessage(QStringLiteral("Opening the database…"),
                           Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
        pump(400);
        shoot(&splash, QStringLiteral("01-splash.png"), QStringLiteral("Splash screen"));
        splash.close();
    }

    // --- composition root -------------------------------------------------------------------
    const QString dataDir = resolveDataDir();
    QDir().mkpath(dataDir);
    QDir().mkpath(dataDir + QStringLiteral("/logs"));
    const QString seedPath = resolveMenuSeedPath();
    say(QStringLiteral("INFO  data dir  %1").arg(dataDir));
    say(QStringLiteral("INFO  menu seed %1").arg(seedPath));

    std::unique_ptr<services::AppContext> ctx;
    try {
        ctx = std::make_unique<services::AppContext>(dataDir, seedPath);
    } catch (const std::exception& e) {
        say(QStringLiteral("FATAL AppContext failed to construct: %1")
                .arg(QString::fromUtf8(e.what())));
        return 2;
    }

    // --- 02 login ---------------------------------------------------------------------------
    {
        auto* login = new gui::LoginWindow(*ctx);
        login->resize(1100, 760);
        login->show();
        pump(450);
        // A filled form reads far more honestly than two empty boxes. Which box is which is
        // decided by echo mode, not by child order — the order is an implementation detail of
        // whatever wrappers the card is built from.
        for (QLineEdit* edit : login->findChildren<QLineEdit*>()) {
            edit->setText(edit->echoMode() == QLineEdit::Normal ? QStringLiteral("admin")
                                                               : QStringLiteral("admin123"));
        }
        if (auto* remember = login->findChild<QCheckBox*>()) remember->setChecked(true);
        pump(150);
        shoot(login, QStringLiteral("02-login.png"), QStringLiteral("Login window"));
        login->close();
        delete login;
    }

    // --- authenticate + populate ------------------------------------------------------------
    // The normal, default path first: sign in WITHOUT "remember me". If that is broken it is a
    // real defect and is reported as such — the fallback below exists only so the remaining
    // screens can still be rendered, never to hide the failure.
    bool loggedIn = false;
    {
        const auto plain =
            ctx->auth().login(QStringLiteral("admin"), QStringLiteral("admin123"), false);
        if (plain.isOk()) {
            loggedIn = true;
            say(QStringLiteral("INFO  signed in as admin (remember-me OFF)"));
        } else {
            say(QStringLiteral("DEFECT  login with rememberMe=false FAILED: %1")
                    .arg(plain.error()));
            const auto remembered =
                ctx->auth().login(QStringLiteral("admin"), QStringLiteral("admin123"), true);
            if (remembered.isOk()) {
                loggedIn = true;
                say(QStringLiteral("DEFECT  the SAME credentials succeed with rememberMe=true — "
                                   "the failure above is a bug in the remember-me OFF branch, not "
                                   "a bad password"));
            } else {
                say(QStringLiteral("FATAL  login failed both ways: %1").arg(remembered.error()));
            }
        }
    }
    if (!loggedIn) return 2;

    const SeededState seeded = seedLiveState(*ctx);

    // --- the shell --------------------------------------------------------------------------
    // No workaround here any more: the StatCard entrance-animation SIGSEGV was fixed at source,
    // so the shell is constructed, shown and animated exactly as the real application does it.
    auto* shell = new gui::MainWindow(*ctx);
    shell->resize(kShotWidth, kShotHeight);
    shell->show();
    // Stat-card entrance is 750 ms and the dashboard count-up is 700 ms behind a per-card stagger,
    // so anything under ~2 s photographs a number mid-count. A screenshot of a half-counted figure
    // is a lie about the data, so wait the animations out properly.
    pump(2400);

    struct Screen {
        int index;
        const char* file;
        const char* label;
    };
    const std::vector<Screen> screens{
        {0, "03-dashboard.png", "Dashboard"},
        {1, "04-menu.png", "Menu"},
        {2, "05-orders-pos.png", "Orders / POS"},
        {3, "07-customers.png", "Customers"},
        {4, "08-employees.png", "Employees"},
        {5, "09-inventory.png", "Inventory"},
        {6, "10-reservations.png", "Reservations"},
        {7, "11-reports.png", "Reports"},
        {8, "12-settings.png", "Settings"},
    };

    for (const Screen& s : screens) {
        QMetaObject::invokeMethod(shell, "onNavigate", Qt::DirectConnection, Q_ARG(int, s.index));
        pump(s.index == 0 ? 2400 : 700);
        if (s.index == 2) {          // POS: put the app-created ticket on screen
            selectOrderRow(shell, seeded.posOrderNumber);
            pump(400);
        }
        if (s.index == 4) {          // Employees: pick a colleague so Attendance has a subject
            selectFirstRow(shell, 7, QStringLiteral("staff"));
            pump(400);
        }
        shoot(shell, QLatin1String(s.file), QLatin1String(s.label));
    }

    // --- 06 billing dialog ------------------------------------------------------------------
    if (seeded.billOrderId > 0) {
        auto* dialog = new gui::BillingDialog(*ctx, seeded.billOrderId, shell);
        dialog->resize(880, 720);
        dialog->show();
        pump(500);
        shoot(dialog, QStringLiteral("06-billing-dialog.png"),
              QStringLiteral("Billing dialog (served order)"));
        dialog->close();
        delete dialog;
    } else {
        ++g_fail;
        g_failures << QStringLiteral("06-billing-dialog.png");
        say(QStringLiteral("FAIL  06-billing-dialog.png — no served order could be prepared"));
    }

    // --- 13 command palette -----------------------------------------------------------------
    {
        auto* palette = new gui::CommandPalette(*ctx, shell);
        palette->resize(820, 520);
        palette->show();
        pump(250);
        if (auto* input = palette->findChild<QLineEdit*>(QStringLiteral("paletteInput"))) {
            input->setText(QStringLiteral("pi"));
        } else if (auto* anyInput = palette->findChild<QLineEdit*>()) {
            anyInput->setText(QStringLiteral("pi"));
        }
        pump(400);
        shoot(palette, QStringLiteral("13-command-palette.png"),
              QStringLiteral("Command palette (Ctrl+K)"));
        palette->close();
        delete palette;
    }

    // --- dark mode --------------------------------------------------------------------------
    gui::ThemeManager::instance().setMode(gui::ThemeManager::Mode::Dark);
    gui::ThemeManager::instance().apply(app);
    pump(600);

    const std::vector<Screen> darkScreens{
        {0, "20-dashboard-dark.png", "Dashboard (dark)"},
        {2, "21-orders-dark.png", "Orders / POS (dark)"},
        {8, "22-settings-dark.png", "Settings (dark)"},
    };
    for (const Screen& s : darkScreens) {
        QMetaObject::invokeMethod(shell, "onNavigate", Qt::DirectConnection, Q_ARG(int, s.index));
        pump(s.index == 0 ? 2400 : 700);
        if (s.index == 2) {
            selectOrderRow(shell, seeded.posOrderNumber);
            pump(400);
        }
        shoot(shell, QLatin1String(s.file), QLatin1String(s.label));
    }

    shell->close();
    delete shell;

    // --- final assertion --------------------------------------------------------------------
    const QStringList expected{
        QStringLiteral("01-splash.png"),        QStringLiteral("02-login.png"),
        QStringLiteral("03-dashboard.png"),     QStringLiteral("04-menu.png"),
        QStringLiteral("05-orders-pos.png"),    QStringLiteral("06-billing-dialog.png"),
        QStringLiteral("07-customers.png"),     QStringLiteral("08-employees.png"),
        QStringLiteral("09-inventory.png"),     QStringLiteral("10-reservations.png"),
        QStringLiteral("11-reports.png"),       QStringLiteral("12-settings.png"),
        QStringLiteral("13-command-palette.png"), QStringLiteral("20-dashboard-dark.png"),
        QStringLiteral("21-orders-dark.png"),   QStringLiteral("22-settings-dark.png"),
    };
    say(QStringLiteral("---- final check ----"));
    int missing = 0;
    for (const QString& name : expected) {
        const QFileInfo info(g_outDir + QLatin1Char('/') + name);
        if (!info.exists() || info.size() <= 4096) {
            ++missing;
            say(QStringLiteral("MISSING/TRIVIAL  %1").arg(name));
        }
    }
    say(QStringLiteral("RESULT  %1 pass, %2 fail, %3 expected files missing or trivial")
            .arg(g_pass)
            .arg(g_fail)
            .arg(missing));
    if (!g_failures.isEmpty())
        say(QStringLiteral("FAILED SHOTS: %1").arg(g_failures.join(QStringLiteral(", "))));

    return (g_fail == 0 && missing == 0) ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("AluChop"));
    QCoreApplication::setOrganizationName(QStringLiteral("AluChop"));
    QCoreApplication::setApplicationVersion(
        QString::fromUtf8(aluchop::core::kAppInfo.version));
    QApplication::setQuitOnLastWindowClosed(false);

    try {
        return run(app);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "uishot: unhandled exception: %s\n", e.what());
        return 3;
    } catch (...) {
        std::fprintf(stderr, "uishot: unhandled unknown exception\n");
        return 3;
    }
}

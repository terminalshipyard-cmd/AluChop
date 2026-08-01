/**
 * @file SettingsPage.cpp
 * @brief Screen 9 — restaurant information, theme, database backup, restore and export.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Everything on this screen is a *request to a service*. The restaurant's identity is written
 * through services::SettingsService's key/value table so receipts and report footers read it from
 * one place; the theme flips gui::ThemeManager live and then persists the choice through the same
 * service; and backup, restore and export are orchestrated by SettingsService over the backup
 * manager down in the persistence layer.
 *
 * @par The widget never copies a file
 * A restore replaces the live database, so it is the most destructive action in the application.
 * This file supplies the warning, the explicit acknowledgement and the toast — and nothing else.
 * The validity check (the SQLite header magic), the swap and the reopen all happen below the
 * service boundary, exactly where the code that understands a database file lives.
 *
 * @par The export bundle
 * "Export everything" writes the five reports as CSV into `exports/` through
 * services::ReportGenerator (whose CSV writer is the persistence-layer `<fstream>` writer) and
 * takes a timestamped `.db` snapshot through SettingsService::createBackup(). The snapshot lands
 * in the backup folder the composition root gave the backup manager, and the page tells the user
 * both paths.
 */

#include "aluchop/gui/SettingsPage.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLatin1Char>
#include <QLatin1String>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

#include "aluchop/core/AppInfo.hpp"
#include "aluchop/core/Result.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/gui/Widgets.hpp"
#include "aluchop/services/AppContext.hpp"

namespace aluchop::gui {
namespace {

// ---------------------------------------------------------------------------
// Presentation helpers
// ---------------------------------------------------------------------------

/// Applies a point-size delta and a weight to a label and gives it a QSS object name.
QLabel* styledLabel(const QString& text, const QString& objectName, QWidget* parent,
                    int pointDelta = 0, QFont::Weight weight = QFont::Normal) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(objectName);
    QFont f = label->font();
    if (pointDelta != 0) f.setPointSize(std::max(8, f.pointSize() + pointDelta));
    f.setWeight(weight);
    label->setFont(f);
    return label;
}

/// A themed button: the object name selects primary / ghost / danger styling from the QSS.
QPushButton* makeButton(const QString& text, const QString& objectName, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(objectName);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(38);
    return button;
}

/// Builds a card surface with a heading and a subtitle, returning the body layout to fill.
QVBoxLayout* buildCard(const QString& title, const QString& subtitle, QWidget* parent,
                       QFrame** outCard) {
    auto* card = new GlassPanel(parent);
    card->setObjectName(QStringLiteral("card"));
    auto* column = new QVBoxLayout(card);
    column->setContentsMargins(20, 18, 20, 18);
    column->setSpacing(12);

    column->addWidget(styledLabel(title, QStringLiteral("sectionTitle"), card, 1, QFont::DemiBold));
    auto* sub = styledLabel(subtitle, QStringLiteral("mutedLabel"), card, 0);
    sub->setWordWrap(true);
    column->addWidget(sub);

    *outCard = card;
    return column;
}

/// A small caption above a form field.
QLabel* fieldCaption(const QString& text, QWidget* parent) {
    return styledLabel(text, QStringLiteral("statCardTitle"), parent, -1, QFont::DemiBold);
}

// ---------------------------------------------------------------------------
// Settings keys — spelled once, exactly as SettingsService documents them
// ---------------------------------------------------------------------------

/// @oop-concept Constants :: the well-known setting keys are named, never retyped as literals
namespace keys {
const QString kName = QStringLiteral("restaurant.name");
const QString kAddress = QStringLiteral("restaurant.address");
const QString kPhone = QStringLiteral("restaurant.phone");
const QString kThemeMode = QStringLiteral("theme.mode");
}  // namespace keys

/// The domains a restore invalidates — every open screen is told to re-read itself.
const QString kAllDomains[] = {
    QStringLiteral("menu"),         QStringLiteral("orders"),   QStringLiteral("customers"),
    QStringLiteral("employees"),    QStringLiteral("inventory"), QStringLiteral("reservations"),
    QStringLiteral("payments"),     QStringLiteral("settings")};

// ---------------------------------------------------------------------------
// Output folders and file names
// ---------------------------------------------------------------------------

/**
 * @brief Resolves (and creates) a writable output folder such as `exports/`.
 *
 * The working directory is tried first because that is the project root when the application is
 * launched by `build.sh`; an installed copy falls back to the executable's folder and finally to
 * the per-user application-data area, so an export can never silently fail for want of a folder.
 */
QString ensureOutputDir(const QString& leaf) {
    QStringList candidates;
    candidates << QDir::current().absoluteFilePath(leaf)
               << QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(leaf)
               << QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                      .absoluteFilePath(leaf);

    for (const QString& candidate : std::as_const(candidates)) {
        QDir dir(candidate);
        if (!dir.exists() && !QDir().mkpath(candidate)) continue;
        const QFileInfo info(candidate);
        if (info.isDir() && info.isWritable()) return QDir(candidate).absolutePath();
    }
    return QDir::tempPath();
}

/// `2026-08-01_1830` — sortable and safe on every file system.
QString fileStamp() {
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmm"));
}

/// A human size for a backup file, e.g. `248 KB`.
QString humanSize(qint64 bytes) {
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

// ---------------------------------------------------------------------------
// Validation — the page refuses to store nonsense
// ---------------------------------------------------------------------------

/// @return an empty string when the restaurant details are usable, otherwise the reason.
QString validateInfo(const QString& name, const QString& address, const QString& phone) {
    if (name.trimmed().size() < 2)
        return QStringLiteral("The restaurant name needs at least two characters — it is printed "
                              "on every receipt.");
    if (name.trimmed().size() > 80)
        return QStringLiteral("The restaurant name is too long for a receipt header (80 characters "
                              "at most).");
    if (address.trimmed().size() > 160)
        return QStringLiteral("The address is too long (160 characters at most).");

    const QString trimmedPhone = phone.trimmed();
    if (!trimmedPhone.isEmpty()) {
        int digits = 0;
        for (const QChar ch : trimmedPhone) {
            if (ch.isDigit()) {
                ++digits;
            } else if (ch != QLatin1Char('+') && ch != QLatin1Char('-') &&
                       ch != QLatin1Char(' ') && ch != QLatin1Char('(') &&
                       ch != QLatin1Char(')')) {
                return QStringLiteral("A phone number may only contain digits, spaces and "
                                      "+ - ( ) characters.");
            }
        }
        if (digits < 7)
            return QStringLiteral("A phone number needs at least seven digits.");
    }
    return QString();
}

/// Shows where files landed: a toast plus a permanent clickable line at the bottom of the page.
void announceSaved(QWidget* page, services::AppContext& ctx, const QString& title,
                   const QString& message, const QString& revealPath) {
    if (auto* status = page->findChild<QLabel*>(QStringLiteral("settingsStatus"))) {
        if (revealPath.isEmpty()) {
            status->setText(message.toHtmlEscaped());
        } else {
            const QFileInfo info(revealPath);
            const QString folder =
                info.isDir() ? info.absoluteFilePath() : info.absolutePath();
            status->setText(QStringLiteral("%1 <a href=\"%2\">%3</a>")
                                .arg(message.toHtmlEscaped(),
                                     QUrl::fromLocalFile(folder).toString().toHtmlEscaped(),
                                     info.absoluteFilePath().toHtmlEscaped()));
        }
        status->setVisible(true);
    }
    ctx.notifications().notify(title, message, 1);
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SettingsPage::SettingsPage(services::AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    setObjectName(QStringLiteral("settingsPage"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(18);

    // --- page header --------------------------------------------------------
    auto* headings = new QVBoxLayout();
    headings->setSpacing(2);
    headings->addWidget(
        styledLabel(pageTitle(), QStringLiteral("pageTitle"), this, 10, QFont::DemiBold));
    headings->addWidget(styledLabel(
        QStringLiteral("Who you are, how the app looks, and how your data is kept safe."),
        QStringLiteral("mutedLabel"), this, 0));
    root->addLayout(headings);

    // --- scrolling body ------------------------------------------------------
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);

    auto* body = new QWidget(scroll);
    body->setAutoFillBackground(false);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(0, 0, 6, 4);
    column->setSpacing(18);

    // --- restaurant information ---------------------------------------------
    QFrame* infoCard = nullptr;
    QVBoxLayout* infoColumn =
        buildCard(QStringLiteral("Restaurant information"),
                  QStringLiteral("Entered once here and reused by every receipt, invoice and "
                                 "report footer."),
                  body, &infoCard);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 4, 0, 0);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(12);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_name = new QLineEdit(infoCard);
    m_name->setPlaceholderText(QStringLiteral("AluChop Restaurant"));
    m_name->setMaxLength(80);
    m_name->setMinimumHeight(38);
    form->addRow(fieldCaption(QStringLiteral("Name"), infoCard), m_name);

    m_address = new QLineEdit(infoCard);
    m_address->setPlaceholderText(QStringLiteral("Street, city"));
    m_address->setMaxLength(160);
    m_address->setMinimumHeight(38);
    form->addRow(fieldCaption(QStringLiteral("Address"), infoCard), m_address);

    m_phone = new QLineEdit(infoCard);
    m_phone->setPlaceholderText(QStringLiteral("+977 …"));
    m_phone->setMaxLength(32);
    m_phone->setMinimumHeight(38);
    form->addRow(fieldCaption(QStringLiteral("Phone"), infoCard), m_phone);

    infoColumn->addLayout(form);

    auto* infoButtons = new QHBoxLayout();
    infoButtons->addStretch(1);
    auto* revertBtn = makeButton(QStringLiteral("Revert"), QStringLiteral("ghostButton"), infoCard);
    revertBtn->setToolTip(QStringLiteral("Discard the edits and read the stored values back."));
    connect(revertBtn, &QPushButton::clicked, this, &SettingsPage::refresh);
    infoButtons->addWidget(revertBtn);
    auto* saveBtn = makeButton(QStringLiteral("Save information"),
                               QStringLiteral("primaryButton"), infoCard);
    connect(saveBtn, &QPushButton::clicked, this, &SettingsPage::onSaveInfo);
    infoButtons->addWidget(saveBtn);
    infoColumn->addLayout(infoButtons);

    column->addWidget(infoCard);

    // --- appearance -----------------------------------------------------------
    QFrame* themeCard = nullptr;
    QVBoxLayout* themeColumn =
        buildCard(QStringLiteral("Appearance"),
                  QStringLiteral("The Sage-Green design system in two moods. The switch is live — "
                                 "the whole stylesheet is regenerated, nothing restarts — and the "
                                 "choice is remembered for the next launch."),
                  body, &themeCard);

    auto* themeRow = new QHBoxLayout();
    themeRow->setSpacing(14);

    auto* themePicker = new QVBoxLayout();
    themePicker->setSpacing(4);
    themePicker->addWidget(fieldCaption(QStringLiteral("Theme"), themeCard));
    m_theme = new QComboBox(themeCard);
    m_theme->setMinimumWidth(180);
    m_theme->setMinimumHeight(38);
    m_theme->setCursor(Qt::PointingHandCursor);
    m_theme->addItem(QStringLiteral("Light"), static_cast<int>(ThemeManager::Mode::Light));
    m_theme->addItem(QStringLiteral("Dark"), static_cast<int>(ThemeManager::Mode::Dark));
    themePicker->addWidget(m_theme);
    themeRow->addLayout(themePicker);

    // A tiny live specimen: the stylesheet restyles it the instant the theme flips, which is a
    // more convincing demonstration than any static swatch.
    auto* preview = new GlassPanel(themeCard);
    preview->setObjectName(QStringLiteral("glassPanel"));
    auto* previewRow = new QHBoxLayout(preview);
    previewRow->setContentsMargins(16, 12, 16, 12);
    previewRow->setSpacing(10);
    previewRow->addWidget(styledLabel(QStringLiteral("Live preview"),
                                      QStringLiteral("statCardTitle"), preview, -1,
                                      QFont::DemiBold));
    auto* previewPrimary = makeButton(QStringLiteral("Primary"), QStringLiteral("primaryButton"),
                                      preview);
    previewPrimary->setEnabled(false);
    previewRow->addWidget(previewPrimary);
    auto* previewGhost = makeButton(QStringLiteral("Secondary"), QStringLiteral("ghostButton"),
                                    preview);
    previewGhost->setEnabled(false);
    previewRow->addWidget(previewGhost);
    auto* previewDanger = makeButton(QStringLiteral("Danger"), QStringLiteral("dangerButton"),
                                     preview);
    previewDanger->setEnabled(false);
    previewRow->addWidget(previewDanger);
    previewRow->addStretch(1);
    themeRow->addWidget(preview, 1);

    themeColumn->addLayout(themeRow);
    column->addWidget(themeCard);

    // --- data safety -----------------------------------------------------------
    QFrame* dataCard = nullptr;
    QVBoxLayout* dataColumn =
        buildCard(QStringLiteral("Database backup, restore and export"),
                  QStringLiteral("A backup is a timestamped copy of the whole SQLite database. A "
                                 "restore validates the file's SQLite signature before it is "
                                 "trusted, then swaps it in — which replaces everything recorded "
                                 "since that snapshot was taken."),
                  body, &dataCard);

    m_backups = new QListWidget(dataCard);
    m_backups->setObjectName(QStringLiteral("backupList"));
    m_backups->setFrameShape(QFrame::NoFrame);
    m_backups->setSelectionMode(QAbstractItemView::SingleSelection);
    m_backups->setMinimumHeight(190);
    m_backups->setAlternatingRowColors(true);
    dataColumn->addWidget(m_backups, 1);

    auto* dataButtons = new QHBoxLayout();
    dataButtons->setSpacing(10);

    auto* backupBtn = makeButton(QStringLiteral("Create backup"), QStringLiteral("primaryButton"),
                                 dataCard);
    backupBtn->setToolTip(QStringLiteral("Take a timestamped snapshot of the live database."));
    connect(backupBtn, &QPushButton::clicked, this, &SettingsPage::onBackup);
    dataButtons->addWidget(backupBtn);

    auto* exportBtn = makeButton(QStringLiteral("Export everything"),
                                 QStringLiteral("ghostButton"), dataCard);
    exportBtn->setToolTip(QStringLiteral("Write all five reports as CSV into exports/ and take a "
                                         "database snapshot alongside them."));
    connect(exportBtn, &QPushButton::clicked, this, &SettingsPage::onExportAll);
    dataButtons->addWidget(exportBtn);

    dataButtons->addStretch(1);

    auto* restoreBtn = makeButton(QStringLiteral("Restore selected backup…"),
                                  QStringLiteral("dangerButton"), dataCard);
    restoreBtn->setToolTip(QStringLiteral("Destructive: replaces the live database with the "
                                          "selected snapshot."));
    connect(restoreBtn, &QPushButton::clicked, this, &SettingsPage::onRestore);
    dataButtons->addWidget(restoreBtn);

    dataColumn->addLayout(dataButtons);
    column->addWidget(dataCard);

    // --- about -----------------------------------------------------------------
    QFrame* aboutCard = nullptr;
    QVBoxLayout* aboutColumn =
        buildCard(QStringLiteral("About AluChop"),
                  QStringLiteral("%1 · version %2")
                      .arg(QString::fromUtf8(core::kAppInfo.appName),
                           QString::fromUtf8(core::kAppInfo.version)),
                  body, &aboutCard);

    auto* credit = styledLabel(QString::fromUtf8(core::kAttributionBlock),
                               QStringLiteral("footerCredit"), aboutCard, 0);
    credit->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    aboutColumn->addWidget(credit);

    auto* copyright = styledLabel(QString::fromUtf8(core::kCopyrightNotice),
                                  QStringLiteral("mutedLabel"), aboutCard, -1);
    copyright->setWordWrap(true);
    aboutColumn->addWidget(copyright);

    column->addWidget(aboutCard);
    column->addStretch(1);

    scroll->setWidget(body);
    root->addWidget(scroll, 1);

    // --- where the last file went ------------------------------------------------
    auto* status = styledLabel(QString(), QStringLiteral("settingsStatus"), this, -1);
    status->setTextFormat(Qt::RichText);
    status->setTextInteractionFlags(Qt::TextBrowserInteraction);
    status->setOpenExternalLinks(true);
    status->setWordWrap(true);
    status->setVisible(false);
    root->addWidget(status);

    // --- wiring -------------------------------------------------------------------
    connect(m_theme, &QComboBox::currentIndexChanged, this, &SettingsPage::onThemeToggled);
    // The theme can also be flipped from Ctrl+T or the command palette; keep the combo honest.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        const QSignalBlocker block(m_theme);
        m_theme->setCurrentIndex(
            ThemeManager::instance().mode() == ThemeManager::Mode::Dark ? 1 : 0);
    });
    connect(m_backups, &QListWidget::itemDoubleClicked, this, &SettingsPage::onRestore);

    refresh();
}

QString SettingsPage::pageTitle() const { return QStringLiteral("Settings"); }

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void SettingsPage::refresh() {
    const Palette& pal = ThemeManager::instance().palette();

    // --- restaurant identity -------------------------------------------------
    try {
        const services::SettingsService& settings = m_ctx.settings();
        m_name->setText(settings.get(keys::kName, QStringLiteral("AluChop Restaurant")));
        m_address->setText(settings.get(keys::kAddress));
        m_phone->setText(settings.get(keys::kPhone));
    } catch (const std::exception& e) {
        m_ctx.notifications().notify(QStringLiteral("Settings"), QString::fromUtf8(e.what()), 3);
    }

    // --- theme ---------------------------------------------------------------
    {
        // Blocked so that merely *reading* the current theme cannot look like the user changing it.
        const QSignalBlocker block(m_theme);
        m_theme->setCurrentIndex(
            ThemeManager::instance().mode() == ThemeManager::Mode::Dark ? 1 : 0);
    }

    // --- backups -------------------------------------------------------------
    m_backups->clear();
    try {
        /// @oop-concept STL (vector) :: the service hands back a plain vector of paths, newest first
        const std::vector<QString> backups = m_ctx.settings().listBackups();

        if (backups.empty()) {
            auto* placeholder = new QListWidgetItem(
                QStringLiteral("No backups yet — press “Create backup” to take the first "
                               "snapshot of your data."),
                m_backups);
            placeholder->setFlags(Qt::NoItemFlags);
            placeholder->setTextAlignment(Qt::AlignCenter);
            placeholder->setForeground(pal.textMuted);
            placeholder->setSizeHint(QSize(0, 96));
        } else {
            for (const QString& path : backups) {
                const QFileInfo info(path);
                auto* item = new QListWidgetItem(m_backups);
                item->setText(QStringLiteral("%1\n%2  ·  %3")
                                  .arg(info.fileName(),
                                       info.lastModified().toString(
                                           QStringLiteral("dddd d MMMM yyyy, hh:mm")),
                                       humanSize(info.size())));
                item->setData(Qt::UserRole, info.absoluteFilePath());
                item->setToolTip(info.absoluteFilePath());
                item->setSizeHint(QSize(0, 58));
            }
            m_backups->setCurrentRow(0);
        }
    } catch (const std::exception& e) {
        auto* placeholder = new QListWidgetItem(QString::fromUtf8(e.what()), m_backups);
        placeholder->setFlags(Qt::NoItemFlags);
        placeholder->setTextAlignment(Qt::AlignCenter);
        placeholder->setForeground(pal.danger);
        placeholder->setSizeHint(QSize(0, 96));
    }
}

// ---------------------------------------------------------------------------
// Restaurant information
// ---------------------------------------------------------------------------

void SettingsPage::onSaveInfo() {
    const QString name = m_name->text().trimmed();
    const QString address = m_address->text().trimmed();
    const QString phone = m_phone->text().trimmed();

    const QString problem = validateInfo(name, address, phone);
    if (!problem.isEmpty()) {
        m_ctx.notifications().notify(QStringLiteral("Check the details"), problem, 2);
        m_name->setFocus();
        return;
    }

    try {
        m_ctx.settings().set(keys::kName, name);
        m_ctx.settings().set(keys::kAddress, address);
        m_ctx.settings().set(keys::kPhone, phone);

        m_ctx.notifications().notify(
            QStringLiteral("Restaurant details saved"),
            QStringLiteral("“%1” will appear on new receipts and report footers.").arg(name), 1);
        m_ctx.notifications().announceDataChanged(QStringLiteral("settings"));
    } catch (const std::exception& e) {
        m_ctx.notifications().notify(QStringLiteral("Could not save the details"),
                                     QString::fromUtf8(e.what()), 3);
    }
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

void SettingsPage::onThemeToggled() {
    const bool dark = m_theme->currentData().toInt() == static_cast<int>(ThemeManager::Mode::Dark);
    const ThemeManager::Mode wanted = dark ? ThemeManager::Mode::Dark : ThemeManager::Mode::Light;

    // Live first: regenerating and re-applying the stylesheet is what makes the switch instant.
    ThemeManager::instance().setMode(wanted);

    try {
        m_ctx.settings().set(keys::kThemeMode,
                             dark ? QStringLiteral("dark") : QStringLiteral("light"));
        m_ctx.notifications().notify(
            QStringLiteral("%1 theme").arg(dark ? QStringLiteral("Dark") : QStringLiteral("Light")),
            QStringLiteral("Applied now and remembered for the next launch."), 1);
    } catch (const std::exception& e) {
        // The theme still changed; only the preference failed to persist, and saying so is honest.
        m_ctx.notifications().notify(
            QStringLiteral("Theme applied but not remembered"),
            QStringLiteral("%1 The next launch will start in the previous theme.")
                .arg(QString::fromUtf8(e.what())),
            2);
    }
}

// ---------------------------------------------------------------------------
// Backup
// ---------------------------------------------------------------------------

void SettingsPage::onBackup() {
    try {
        /// @oop-concept Class Template (used) :: the backup path comes home in core::Result<QString>
        const core::Result<QString> result = m_ctx.settings().createBackup();
        if (result.isErr()) {
            m_ctx.notifications().notify(QStringLiteral("Backup failed"), result.error(), 3);
            return;
        }

        refresh();
        announceSaved(this, m_ctx, QStringLiteral("Backup created"),
                      QStringLiteral("A snapshot of the database was written to"), result.value());
    } catch (const std::exception& e) {
        m_ctx.notifications().notify(QStringLiteral("Backup failed"), QString::fromUtf8(e.what()),
                                     3);
    }
}

// ---------------------------------------------------------------------------
// Restore — the destructive one
// ---------------------------------------------------------------------------

void SettingsPage::onRestore() {
    QListWidgetItem* selected = m_backups->currentItem();
    const QString path = selected ? selected->data(Qt::UserRole).toString() : QString();
    if (path.isEmpty()) {
        m_ctx.notifications().notify(
            QStringLiteral("Pick a backup first"),
            QStringLiteral("Select the snapshot you want to restore from the list."), 2);
        return;
    }

    const QFileInfo info(path);
    const Palette& pal = ThemeManager::instance().palette();

    // ---- the confirmation step ------------------------------------------------
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Restore database"));
    dialog.setMinimumWidth(560);

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(24, 22, 24, 20);
    column->setSpacing(12);

    auto* headline = styledLabel(QStringLiteral("This will replace your live data"),
                                 QStringLiteral("sectionTitle"), &dialog, 6, QFont::DemiBold);
    QPalette headlinePalette = headline->palette();
    headlinePalette.setColor(QPalette::WindowText, pal.danger);
    headline->setPalette(headlinePalette);
    column->addWidget(headline);

    auto* detail = styledLabel(
        QStringLiteral(
            "Restoring rolls the whole database back to <b>%1</b>, taken on %2 (%3).<br><br>"
            "Every order, payment, reservation, stock movement and customer record created "
            "<i>after</i> that moment will be gone. The file's SQLite signature is checked before "
            "it is trusted, and a safety snapshot of the current database is taken automatically "
            "first — but the live data is still replaced.")
            .arg(info.fileName().toHtmlEscaped(),
                 info.lastModified().toString(QStringLiteral("dddd d MMMM yyyy, hh:mm")),
                 humanSize(info.size())),
        QStringLiteral("mutedLabel"), &dialog, 0);
    detail->setTextFormat(Qt::RichText);
    detail->setWordWrap(true);
    column->addWidget(detail);

    auto* acknowledge = new QCheckBox(
        QStringLiteral("I understand the current database will be replaced."), &dialog);
    acknowledge->setCursor(Qt::PointingHandCursor);
    column->addWidget(acknowledge);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* cancelBtn = makeButton(QStringLiteral("Cancel"), QStringLiteral("ghostButton"), &dialog);
    cancelBtn->setDefault(true);
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    buttons->addWidget(cancelBtn);

    auto* confirmBtn = makeButton(QStringLiteral("Restore and overwrite"),
                                  QStringLiteral("dangerButton"), &dialog);
    confirmBtn->setEnabled(false);
    connect(acknowledge, &QCheckBox::toggled, confirmBtn, &QPushButton::setEnabled);
    connect(confirmBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttons->addWidget(confirmBtn);
    column->addLayout(buttons);

    if (dialog.exec() != QDialog::Accepted) return;

    // ---- safety snapshot, then the restore --------------------------------------
    QString safetyPath;
    try {
        const core::Result<QString> safety = m_ctx.settings().createBackup();
        if (safety.isOk()) safetyPath = safety.value();
    } catch (const std::exception&) {
        // A failed safety snapshot must not block a deliberate restore; it is reported below.
    }

    try {
        const core::Result<void> result = m_ctx.settings().restoreBackup(path);
        if (result.isErr()) {
            m_ctx.notifications().notify(QStringLiteral("Restore failed"), result.error(), 3);
            return;
        }
    } catch (const std::exception& e) {
        m_ctx.notifications().notify(QStringLiteral("Restore failed"), QString::fromUtf8(e.what()),
                                     3);
        return;
    }

    // Every screen is now looking at data that no longer exists — tell all of them.
    for (const QString& domain : kAllDomains) m_ctx.notifications().announceDataChanged(domain);

    refresh();

    const QString message =
        safetyPath.isEmpty()
            ? QStringLiteral("The database was restored from %1.").arg(info.fileName())
            : QStringLiteral("The database was restored from %1. A safety snapshot of the "
                             "previous data was kept at")
                  .arg(info.fileName());
    announceSaved(this, m_ctx, QStringLiteral("Database restored"), message, safetyPath);
}

// ---------------------------------------------------------------------------
// Export everything
// ---------------------------------------------------------------------------

void SettingsPage::onExportAll() {
    const QString folder = QDir(ensureOutputDir(QStringLiteral("exports")))
                               .filePath(QStringLiteral("aluchop-export-%1").arg(fileStamp()));
    if (!QDir(folder).exists() && !QDir().mkpath(folder)) {
        m_ctx.notifications().notify(
            QStringLiteral("Export failed"),
            QStringLiteral("Could not create the export folder %1.").arg(folder), 3);
        return;
    }

    // The full history: every report that takes a range gets one, the rest are "as things stand".
    const QDate to = QDate::currentDate();
    const QDate from = to.addDays(-364);

    struct Bundle {
        services::ReportKind kind;
        const char* stem;
    };
    const Bundle bundle[] = {{services::ReportKind::Sales, "sales"},
                             {services::ReportKind::Orders, "orders"},
                             {services::ReportKind::Inventory, "inventory"},
                             {services::ReportKind::Customers, "customers"},
                             {services::ReportKind::Employees, "employees"}};

    int written = 0;
    QStringList failures;

    for (const Bundle& entry : bundle) {
        try {
            /// @oop-concept Runtime Polymorphism :: five different reports, one abstract call —
            /// the loop below never asks which concrete generator it is holding.
            const std::unique_ptr<services::ReportGenerator> report =
                m_ctx.reports().makeReport(entry.kind, from, to);
            if (!report) {
                failures << QString::fromUtf8(entry.stem);
                continue;
            }
            report->exportCsv(
                QDir(folder).filePath(QStringLiteral("%1.csv").arg(QString::fromUtf8(entry.stem))));
            ++written;
        } catch (const std::exception&) {
            failures << QString::fromUtf8(entry.stem);
        }
    }

    // A CSV bundle is readable forever; the .db snapshot is what can actually be restored.
    QString snapshot;
    try {
        const core::Result<QString> backup = m_ctx.settings().createBackup();
        if (backup.isOk()) snapshot = backup.value();
    } catch (const std::exception&) {
        // Reported through the summary below.
    }

    refresh();

    if (written == 0) {
        m_ctx.notifications().notify(
            QStringLiteral("Export failed"),
            QStringLiteral("None of the reports could be written to %1.").arg(folder), 3);
        return;
    }

    QString message = QStringLiteral("%1 of 5 report files written to").arg(written);
    if (!failures.isEmpty())
        message = QStringLiteral("%1 of 5 report files written (%2 failed) to")
                      .arg(written)
                      .arg(failures.join(QStringLiteral(", ")));

    announceSaved(this, m_ctx, QStringLiteral("Export complete"), message, folder);

    if (!snapshot.isEmpty()) {
        m_ctx.notifications().notify(
            QStringLiteral("Database snapshot"),
            QStringLiteral("A restorable copy of the database was written to %1.").arg(snapshot), 1);
    } else {
        m_ctx.notifications().notify(
            QStringLiteral("Database snapshot skipped"),
            QStringLiteral("The CSV bundle was written, but the .db snapshot could not be taken."),
            2);
    }

    try {
        m_ctx.audit().log(QStringLiteral("EXPORT_ALL"), QStringLiteral("database"));
    } catch (const std::exception&) {
        // An unwritable audit trail is surfaced by the Reports screen's integrity check.
    }
}

} // namespace aluchop::gui

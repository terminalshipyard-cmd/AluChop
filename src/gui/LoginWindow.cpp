/**
 * @file LoginWindow.cpp
 * @brief Implementation of the sign-in window and its inline password-recovery flow.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * One card, centred on a tinted sage backdrop, containing a QStackedWidget with exactly two
 * faces: "sign in" and "reset password". Everything the user can do here is a call into
 * services::AuthService — this file knows nothing about users, hashes, tables or SQL, and it
 * never catches an exception because AuthService hands back a core::Result instead.
 *
 * Failures are reported *inline*, in a themed error strip under the fields, never as a modal
 * pop-up: a password typo should not cost the user a dialog dismissal.
 */

#include "aluchop/gui/LoginWindow.hpp"

#include "aluchop/core/AppInfo.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/models/User.hpp"
#include "aluchop/services/AppContext.hpp"
#include "aluchop/services/AuthService.hpp"

#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRectF>
#include <QScreen>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtSvg/QSvgRenderer>

namespace aluchop::gui {
namespace {

/// Renders one of the few glyphs this window needs, from generated SVG source.
/// @param body SVG element markup drawn with `fill:none; stroke:<colour>`.
QPixmap glyph(const QString& body, const QColor& colour, int px, qreal strokeWidth = 1.7) {
    const qreal dpr = 2.0;
    QPixmap pm(static_cast<int>(px * dpr), static_cast<int>(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QString doc =
        QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
                       "stroke='%1' stroke-width='%2' stroke-linecap='round' "
                       "stroke-linejoin='round'>%3</svg>")
            .arg(colour.name(QColor::HexRgb), QString::number(strokeWidth, 'f', 2), body);

    QSvgRenderer renderer(doc.toUtf8());
    if (renderer.isValid()) {
        renderer.render(&painter, QRectF(0, 0, px, px));
    }
    return pm;
}

const QString kBrandBody = QStringLiteral(
    "<path d='M7.4 20.6h9.2v-3.9H7.4z'/>"
    "<path d='M7.4 16.7c-2.6-.5-4.3-2.6-4.3-5.1 0-2.9 2.3-5.2 5.2-5.2"
    ".7-2 2.6-3.4 4.8-3.4s4.1 1.4 4.8 3.4c2.9 0 5.2 2.3 5.2 5.2 0 2.5-1.7 4.6-4.3 5.1'/>"
    "<path d='M10 16.7v-3.4M14 16.7v-3.4'/>");

const QString kEyeBody = QStringLiteral(
    "<path d='M2.6 12S6.2 5.6 12 5.6 21.4 12 21.4 12 17.8 18.4 12 18.4 2.6 12 2.6 12z'/>"
    "<circle cx='12' cy='12' r='3'/>");

const QString kEyeOffBody = QStringLiteral(
    "<path d='M2.6 12S6.2 5.6 12 5.6 21.4 12 21.4 12 17.8 18.4 12 18.4 2.6 12 2.6 12z'/>"
    "<circle cx='12' cy='12' r='3'/><path d='M4 4l16 16'/>");

/// Builds the little caption that sits above each field.
QLabel* fieldCaption(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text.toUpper(), parent);
    label->setObjectName(QStringLiteral("statCardTitle"));   // same quiet, tracked caption style
    return label;
}

} // namespace

/// @oop-concept Dynamic Memory Allocation :: every widget below is `new`-ed and handed to a Qt
/// parent (directly or through a layout); nothing here is ever manually deleted
LoginWindow::LoginWindow(services::AppContext& ctx, QWidget* parent)
    : QWidget(parent), m_ctx(ctx) {
    setWindowTitle(QStringLiteral("AluChop — Sign in"));
    setMinimumSize(760, 560);
    resize(960, 640);

    const Palette& p = ThemeManager::instance().palette();

    // --- backdrop -------------------------------------------------------------------------
    // A plain QWidget does not paint a style-sheet background (ThemeManager.hpp), so the tinted
    // gradient lives on a QFrame that fills the window.
    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);

    auto* backdrop = new QFrame(this);
    backdrop->setObjectName(QStringLiteral("loginBackdrop"));
    backdrop->setFrameShape(QFrame::NoFrame);
    shell->addWidget(backdrop);

    auto* centring = new QVBoxLayout(backdrop);
    centring->setContentsMargins(24, 24, 24, 24);
    centring->setAlignment(Qt::AlignCenter);

    // --- the card -------------------------------------------------------------------------
    auto* card = new QFrame(backdrop);
    card->setObjectName(QStringLiteral("glassPanel"));
    card->setFrameShape(QFrame::NoFrame);
    card->setFixedWidth(430);

    auto* shadow = new QGraphicsDropShadowEffect(card);
    QColor shadowColour = p.shadow;
    shadowColour.setAlphaF(0.22f);
    shadow->setColor(shadowColour);
    shadow->setBlurRadius(46);
    shadow->setOffset(0, 14);
    card->setGraphicsEffect(shadow);

    centring->addWidget(card, 0, Qt::AlignCenter);

    auto* column = new QVBoxLayout(card);
    column->setContentsMargins(36, 34, 36, 28);
    column->setSpacing(0);

    // --- wordmark -------------------------------------------------------------------------
    auto* brandRow = new QHBoxLayout();
    brandRow->setContentsMargins(0, 0, 0, 0);
    brandRow->setSpacing(11);

    auto* mark = new QLabel(card);
    mark->setObjectName(QStringLiteral("brandMark"));
    mark->setFixedSize(34, 34);
    mark->setAlignment(Qt::AlignCenter);
    mark->setPixmap(glyph(kBrandBody, p.primary, 30));

    auto* brand = new QLabel(QStringLiteral("AluChop"), card);
    brand->setObjectName(QStringLiteral("brandLabel"));

    brandRow->addStretch(1);
    brandRow->addWidget(mark, 0, Qt::AlignVCenter);
    brandRow->addWidget(brand, 0, Qt::AlignVCenter);
    brandRow->addStretch(1);
    column->addLayout(brandRow);

    auto* welcome = new QLabel(QStringLiteral("Sign in to open the restaurant"), card);
    welcome->setObjectName(QStringLiteral("mutedLabel"));
    welcome->setAlignment(Qt::AlignCenter);
    column->addSpacing(6);
    column->addWidget(welcome);
    column->addSpacing(26);

    // --- the two faces of the card ---------------------------------------------------------
    auto* stack = new QStackedWidget(card);
    stack->setObjectName(QStringLiteral("loginStack"));
    column->addWidget(stack);

    // ===================== face 0 — sign in ==================================================
    auto* signIn = new QWidget(stack);
    auto* form = new QVBoxLayout(signIn);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(7);

    form->addWidget(fieldCaption(QStringLiteral("Username"), signIn));
    m_username = new QLineEdit(signIn);
    m_username->setPlaceholderText(QStringLiteral("Your username or staff id"));
    m_username->setMinimumHeight(42);
    form->addWidget(m_username);

    form->addSpacing(9);
    form->addWidget(fieldCaption(QStringLiteral("Password"), signIn));
    m_password = new QLineEdit(signIn);
    m_password->setPlaceholderText(QStringLiteral("Your password"));
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setMinimumHeight(42);
    form->addWidget(m_password);

    // Reveal toggle lives inside the field, the way every modern sign-in form does it.
    QAction* reveal = m_password->addAction(QIcon(glyph(kEyeBody, p.textMuted, 18)),
                                            QLineEdit::TrailingPosition);
    reveal->setCheckable(true);
    reveal->setToolTip(QStringLiteral("Show password"));
    connect(reveal, &QAction::toggled, this, [this, reveal](bool shown) {
        const Palette& live = ThemeManager::instance().palette();
        m_password->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
        reveal->setIcon(QIcon(glyph(shown ? kEyeOffBody : kEyeBody, live.textMuted, 18)));
        reveal->setToolTip(shown ? QStringLiteral("Hide password")
                                 : QStringLiteral("Show password"));
    });

    form->addSpacing(12);
    auto* optionsRow = new QHBoxLayout();
    optionsRow->setContentsMargins(0, 0, 0, 0);

    m_remember = new QCheckBox(QStringLiteral("Remember me on this device"), signIn);
    m_remember->setCursor(Qt::PointingHandCursor);

    auto* forgot = new QPushButton(QStringLiteral("Forgot password?"), signIn);
    forgot->setObjectName(QStringLiteral("linkButton"));
    forgot->setCursor(Qt::PointingHandCursor);
    forgot->setFlat(true);

    optionsRow->addWidget(m_remember);
    optionsRow->addStretch(1);
    optionsRow->addWidget(forgot);
    form->addLayout(optionsRow);

    form->addSpacing(18);
    m_loginBtn = new QPushButton(QStringLiteral("Sign in"), signIn);
    m_loginBtn->setObjectName(QStringLiteral("primaryButton"));
    m_loginBtn->setCursor(Qt::PointingHandCursor);
    m_loginBtn->setMinimumHeight(44);
    m_loginBtn->setDefault(true);
    form->addWidget(m_loginBtn);

    stack->addWidget(signIn);

    // ===================== face 1 — reset password ===========================================
    auto* reset = new QWidget(stack);
    reset->setObjectName(QStringLiteral("resetPanel"));
    reset->setProperty("step", 1);

    auto* resetColumn = new QVBoxLayout(reset);
    resetColumn->setContentsMargins(0, 0, 0, 0);
    resetColumn->setSpacing(7);

    auto* resetTitle = new QLabel(QStringLiteral("Reset your password"), reset);
    resetTitle->setObjectName(QStringLiteral("sectionTitle"));
    resetColumn->addWidget(resetTitle);

    auto* resetHint = new QLabel(
        QStringLiteral("Answer the security question you chose when the account was created."),
        reset);
    resetHint->setObjectName(QStringLiteral("mutedLabel"));
    resetHint->setWordWrap(true);
    resetColumn->addWidget(resetHint);
    resetColumn->addSpacing(14);

    resetColumn->addWidget(fieldCaption(QStringLiteral("Username"), reset));
    auto* resetUser = new QLineEdit(reset);
    resetUser->setObjectName(QStringLiteral("resetUser"));
    resetUser->setPlaceholderText(QStringLiteral("Account to recover"));
    resetUser->setMinimumHeight(42);
    resetColumn->addWidget(resetUser);

    auto* question = new QLabel(reset);
    question->setObjectName(QStringLiteral("resetQuestion"));
    question->setWordWrap(true);
    question->setVisible(false);
    resetColumn->addSpacing(12);
    resetColumn->addWidget(question);

    auto* answer = new QLineEdit(reset);
    answer->setObjectName(QStringLiteral("resetAnswer"));
    answer->setPlaceholderText(QStringLiteral("Your answer"));
    answer->setMinimumHeight(42);
    answer->setVisible(false);
    resetColumn->addWidget(answer);

    auto* newPass = new QLineEdit(reset);
    newPass->setObjectName(QStringLiteral("resetNew"));
    newPass->setPlaceholderText(QStringLiteral("New password (at least 6 characters)"));
    newPass->setEchoMode(QLineEdit::Password);
    newPass->setMinimumHeight(42);
    newPass->setVisible(false);
    resetColumn->addSpacing(7);
    resetColumn->addWidget(newPass);

    auto* confirmPass = new QLineEdit(reset);
    confirmPass->setObjectName(QStringLiteral("resetConfirm"));
    confirmPass->setPlaceholderText(QStringLiteral("Repeat the new password"));
    confirmPass->setEchoMode(QLineEdit::Password);
    confirmPass->setMinimumHeight(42);
    confirmPass->setVisible(false);
    resetColumn->addSpacing(7);
    resetColumn->addWidget(confirmPass);

    resetColumn->addSpacing(18);
    auto* resetButtons = new QHBoxLayout();
    resetButtons->setContentsMargins(0, 0, 0, 0);
    resetButtons->setSpacing(10);

    auto* back = new QPushButton(QStringLiteral("Back"), reset);
    back->setObjectName(QStringLiteral("ghostButton"));
    back->setCursor(Qt::PointingHandCursor);
    back->setMinimumHeight(42);

    auto* proceed = new QPushButton(QStringLiteral("Continue"), reset);
    proceed->setObjectName(QStringLiteral("primaryButton"));   // keeps the filled house style
    proceed->setProperty("resetRole", QStringLiteral("action"));
    proceed->setCursor(Qt::PointingHandCursor);
    proceed->setMinimumHeight(42);

    resetButtons->addWidget(back);
    resetButtons->addWidget(proceed, 1);
    resetColumn->addLayout(resetButtons);

    stack->addWidget(reset);

    // --- shared status strips ---------------------------------------------------------------
    m_error = new QLabel(card);
    m_error->setObjectName(QStringLiteral("errorLabel"));
    m_error->setWordWrap(true);
    m_error->setVisible(false);

    auto* success = new QLabel(card);
    success->setObjectName(QStringLiteral("successLabel"));
    success->setWordWrap(true);
    success->setVisible(false);

    column->addSpacing(14);
    column->addWidget(m_error);
    column->addWidget(success);

    // --- SPEC §10 credit ----------------------------------------------------------------------
    auto* credit = new QLabel(
        QStringLiteral("%1 · %2")
            .arg(QString::fromUtf8(core::kAppInfo.developer),
                 QString::fromUtf8(core::kAppInfo.rollNo)),
        card);
    credit->setObjectName(QStringLiteral("footerCredit"));
    credit->setAlignment(Qt::AlignCenter);
    column->addSpacing(20);
    column->addWidget(credit);

    // --- wiring ---------------------------------------------------------------------------
    connect(m_loginBtn, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
    connect(m_password, &QLineEdit::returnPressed, this, &LoginWindow::onLoginClicked);
    connect(m_username, &QLineEdit::returnPressed, this,
            [this]() { m_password->setFocus(Qt::OtherFocusReason); });
    connect(m_username, &QLineEdit::textEdited, this, [this]() { setError(QString()); });
    connect(m_password, &QLineEdit::textEdited, this, [this]() { setError(QString()); });

    connect(forgot, &QPushButton::clicked, this, &LoginWindow::onForgotPassword);
    connect(proceed, &QPushButton::clicked, this, &LoginWindow::onForgotPassword);
    connect(resetUser, &QLineEdit::returnPressed, this, &LoginWindow::onForgotPassword);
    connect(answer, &QLineEdit::returnPressed, this, &LoginWindow::onForgotPassword);
    connect(confirmPass, &QLineEdit::returnPressed, this, &LoginWindow::onForgotPassword);

    connect(back, &QPushButton::clicked, this, [this, stack, reset, question, answer, newPass,
                                                confirmPass, proceed, success]() {
        // Rewind the flow completely: an abandoned reset must not leave a half-answered form.
        reset->setProperty("step", 1);
        question->setVisible(false);
        answer->setVisible(false);
        answer->clear();
        newPass->setVisible(false);
        newPass->clear();
        confirmPass->setVisible(false);
        confirmPass->clear();
        proceed->setText(QStringLiteral("Continue"));
        success->setVisible(false);
        setError(QString());
        stack->setCurrentIndex(0);
        m_username->setFocus(Qt::OtherFocusReason);
    });

    // Re-tint the generated pixmaps when the theme changes; the stylesheet handles the rest.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [mark, reveal]() {
                const Palette& live = ThemeManager::instance().palette();
                mark->setPixmap(glyph(kBrandBody, live.primary, 30));
                reveal->setIcon(QIcon(glyph(reveal->isChecked() ? kEyeOffBody : kEyeBody,
                                            live.textMuted, 18)));
            });

    // Centre on the screen the cursor is on, and land the caret in the first field.
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        move(available.center().x() - width() / 2, available.center().y() - height() / 2);
    }
    QTimer::singleShot(0, this, [this]() { m_username->setFocus(Qt::OtherFocusReason); });
}

void LoginWindow::onLoginClicked() {
    const QString username = m_username->text().trimmed();
    const QString password = m_password->text();

    // Cheap local validation first, so an empty form never costs a database round trip.
    if (username.isEmpty()) {
        setError(QStringLiteral("Enter your username to continue."));
        m_username->setFocus(Qt::OtherFocusReason);
        return;
    }
    if (password.isEmpty()) {
        setError(QStringLiteral("Enter your password to continue."));
        m_password->setFocus(Qt::OtherFocusReason);
        return;
    }

    m_loginBtn->setEnabled(false);
    m_loginBtn->setText(QStringLiteral("Signing in…"));

    // AuthService converts its internal AuthException into a Result at the boundary, which is
    // precisely why this GUI code has no try block.
    const core::Result<models::User> result =
        m_ctx.auth().login(username, password, m_remember->isChecked());

    m_loginBtn->setEnabled(true);
    m_loginBtn->setText(QStringLiteral("Sign in"));

    if (!result) {
        setError(result.error());
        m_password->clear();
        m_password->setFocus(Qt::OtherFocusReason);
        return;
    }

    setError(QString());
    emit loggedIn();
}

void LoginWindow::onForgotPassword() {
    auto* stack = findChild<QStackedWidget*>(QStringLiteral("loginStack"));
    auto* panel = findChild<QWidget*>(QStringLiteral("resetPanel"));
    auto* userEdit = findChild<QLineEdit*>(QStringLiteral("resetUser"));
    auto* question = findChild<QLabel*>(QStringLiteral("resetQuestion"));
    auto* answer = findChild<QLineEdit*>(QStringLiteral("resetAnswer"));
    auto* newPass = findChild<QLineEdit*>(QStringLiteral("resetNew"));
    auto* confirmPass = findChild<QLineEdit*>(QStringLiteral("resetConfirm"));
    auto* success = findChild<QLabel*>(QStringLiteral("successLabel"));
    if (!stack || !panel || !userEdit || !question || !answer || !newPass || !confirmPass
        || !success) {
        return;
    }
    // Identified by its role property, not by position or objectName: `back` is created first, so
    // a bare findChild<QPushButton*>() would return the wrong button, and both buttons share the
    // objectName space with the rest of the application's themed buttons.
    QPushButton* proceed = nullptr;
    for (QPushButton* candidate : panel->findChildren<QPushButton*>()) {
        if (candidate->property("resetRole").toString() == QLatin1String("action")) {
            proceed = candidate;
            break;
        }
    }

    // One slot drives all three steps; the current step is carried on the panel itself.
    const int step = panel->property("step").toInt();

    // ---- step 1 entry: reveal the reset face -------------------------------------------
    if (stack->currentIndex() == 0) {
        setError(QString());
        success->setVisible(false);
        userEdit->setText(m_username->text().trimmed());
        stack->setCurrentIndex(1);
        userEdit->setFocus(Qt::OtherFocusReason);
        return;
    }

    // ---- step 1: look the security question up ------------------------------------------
    if (step == 1) {
        const QString username = userEdit->text().trimmed();
        if (username.isEmpty()) {
            setError(QStringLiteral("Enter the username of the account to recover."));
            return;
        }

        const core::Result<QString> found = m_ctx.auth().securityQuestionFor(username);
        if (!found) {
            setError(found.error());
            return;
        }

        setError(QString());
        question->setText(QStringLiteral("Security question:  %1").arg(found.value()));
        question->setVisible(true);
        answer->setVisible(true);
        newPass->setVisible(true);
        confirmPass->setVisible(true);
        answer->setFocus(Qt::OtherFocusReason);
        panel->setProperty("step", 2);
        if (proceed) {
            proceed->setText(QStringLiteral("Reset password"));
        }
        return;
    }

    // ---- step 2: verify the answer and set the new password -----------------------------
    const QString username = userEdit->text().trimmed();
    const QString given = answer->text().trimmed();
    const QString fresh = newPass->text();

    if (given.isEmpty()) {
        setError(QStringLiteral("Answer the security question to continue."));
        return;
    }
    if (fresh != confirmPass->text()) {
        setError(QStringLiteral("The two new passwords do not match."));
        return;
    }

    const core::Result<void> done = m_ctx.auth().resetPasswordWithAnswer(username, given, fresh);
    if (!done) {
        setError(done.error());
        return;
    }

    // ---- step 3: back to sign-in, with the good news kept on screen ----------------------
    setError(QString());
    panel->setProperty("step", 1);
    question->setVisible(false);
    answer->clear();
    answer->setVisible(false);
    newPass->clear();
    newPass->setVisible(false);
    confirmPass->clear();
    confirmPass->setVisible(false);
    if (proceed) {
        proceed->setText(QStringLiteral("Continue"));
    }

    stack->setCurrentIndex(0);
    m_username->setText(username);
    m_password->clear();
    m_password->setFocus(Qt::OtherFocusReason);

    success->setText(QStringLiteral("Password updated. Sign in with your new password."));
    success->setVisible(true);
}

void LoginWindow::setError(const QString& message) {
    m_error->setText(message);
    m_error->setVisible(!message.isEmpty());

    if (!message.isEmpty()) {
        if (auto* success = findChild<QLabel*>(QStringLiteral("successLabel"))) {
            success->setVisible(false);   // never show a failure and a success side by side
        }
    }
}

} // namespace aluchop::gui

#pragma once

/**
 * @file LoginWindow.hpp
 * @brief Authentication window: admin/employee sign-in, remember me, forgot password.
 * @author Shashank Bhattarai (ACE082BCT078)
 */

#include <QString>
#include <QWidget>

namespace aluchop::services {
class AppContext;
}

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace aluchop::gui {

/**
 * @brief Frameless-feeling sign-in card centred on a sage backdrop.
 *
 * Talks to exactly one service — services::AuthService, reached through the composition root.
 * Passwords never leave this widget in plaintext beyond the call into
 * AuthService::login(); hashing (salted SHA-256) happens in the service layer, and this class
 * has no knowledge of users, tables or SQL.
 *
 * Flows implemented:
 *  - **Login**: username + password → AuthService::login(); a core::Result error becomes the
 *    red error label, success emits loggedIn().
 *  - **Remember me**: the checkbox is passed straight through to AuthService::login(), which
 *    persists a remember token; main.cpp consults AuthService::tryRememberedLogin() on the
 *    next start and may skip this window entirely.
 *  - **Forgot password**: an inline three-step flow inside the same card —
 *    username → AuthService::securityQuestionFor() → answer + new password →
 *    AuthService::resetPasswordWithAnswer(). No email, no dialogs stacked on dialogs.
 *
 * Role-based authorisation is *not* decided here: AuthService records the signed-in
 * models::User, and MainWindow/pages ask AuthService::hasRole() when enabling actions.
 *
 * @par Ownership
 * Created on the heap with no parent by main.cpp with @c Qt::WA_DeleteOnClose
 * (ARCHITECTURE §9 rule 2). Its child widgets are Qt-parent-owned raw pointers.
 *
 * @oop-concept Dynamic Memory Allocation :: every child widget is `new`-ed and owned by its
 *              Qt parent — the idiomatic Qt lifetime model, no manual delete anywhere
 */
class LoginWindow : public QWidget {
    Q_OBJECT
public:
    /// @param ctx composition root; only AppContext::auth() is used.
    /// @param parent normally null — this is a top-level window.
    explicit LoginWindow(services::AppContext& ctx, QWidget* parent = nullptr);

signals:
    /// Emitted once AuthService has accepted the credentials. main.cpp reacts by building the
    /// MainWindow and closing this window; the window is never reused after this.
    void loggedIn();

private slots:
    /// Validates the fields, calls AuthService::login(username, password, rememberMe) and
    /// either shows the error text or emits loggedIn(). Also bound to Return in both fields.
    void onLoginClicked();

    /// Drives the inline three-step password reset described in the class documentation.
    void onForgotPassword();

private:
    /// Shows @p message in the error label (empty string hides it) — one place decides how
    /// failures look, so both slots stay short.
    /// @param message user-facing text; never contains internals or SQL.
    void setError(const QString& message);

    services::AppContext& m_ctx;         ///< Composition root, outlives this window.
    QLineEdit* m_username = nullptr;     ///< Username / staff id field.
    QLineEdit* m_password = nullptr;     ///< Password field (QLineEdit::Password echo mode).
    QCheckBox* m_remember = nullptr;     ///< "Remember me on this device".
    QPushButton* m_loginBtn = nullptr;   ///< Primary action (objectName "primaryButton").
    QLabel* m_error = nullptr;           ///< Inline error line, Palette::danger, hidden when empty.
};

} // namespace aluchop::gui

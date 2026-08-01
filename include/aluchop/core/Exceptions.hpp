#pragma once
/**
 * @file Exceptions.hpp
 * @brief The AluChop custom exception hierarchy (SPEC §5.8).
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * One catchable base — AluChopException — with five domain children. Layers
 * throw the child that matches the failure they detected; `main()` and the
 * service boundary catch either the precise child (to recover differently per
 * kind) or the base (as a last-resort handler).
 *
 * Every exception can additionally carry *context*: the offending field, SQL
 * statement, file path or username that caused the failure, plus a numeric
 * error code (e.g. a SQLite result code). Both are folded into what().
 *
 * Header-only: exceptions must be usable from every layer without linking.
 */

#include <stdexcept>
#include <string>
#include <utility>

namespace aluchop::core {

/**
 * @brief Base class of every exception thrown by AluChop.
 *
 * Derives from std::runtime_error so that generic `catch (const std::exception&)`
 * handlers (including Qt's and the standard library's) still work, while
 * `catch (const AluChopException&)` isolates *our* failures from everyone else's.
 */
/// @oop-concept Custom Exception Hierarchy :: one catchable base for the whole application
class AluChopException : public std::runtime_error {
public:
    /**
     * @brief Constructs an exception carrying only a message.
     * @param what Human-readable description of the failure.
     */
    explicit AluChopException(const std::string& what)
        : std::runtime_error(what), m_message(what), m_full(what) {}

    /**
     * @brief Constructs an exception carrying a message, context and error code.
     * @param what    Human-readable description of the failure.
     * @param context The offending detail: field name, file path, SQL, username…
     * @param code    Optional numeric code (e.g. a SQLite result code); 0 means "none".
     */
    AluChopException(const std::string& what, std::string context, int code = 0)
        : std::runtime_error(what),
          m_message(what),
          m_full(compose(what, context, code)),
          m_context(std::move(context)),
          m_code(code) {}

    /**
     * @brief Polymorphic destructor for a base that is thrown by value and caught by reference.
     */
    ~AluChopException() override = default;

    /**
     * @brief The complete diagnostic text: message + context + code.
     * @return NUL-terminated string owned by this exception object.
     */
    /// @oop-concept Method Overriding :: what() is overridden so context and code travel with the throw
    const char* what() const noexcept override { return m_full.c_str(); }

    /**
     * @brief The message without the appended context/code decoration.
     * @return Reference to the stored message.
     */
    const std::string& message() const noexcept { return m_message; }

    /**
     * @brief The offending detail supplied at throw time.
     * @return Reference to the context string; empty when none was given.
     */
    const std::string& context() const noexcept { return m_context; }

    /**
     * @brief The numeric error code supplied at throw time.
     * @return The code, or 0 when none was given.
     */
    int code() const noexcept { return m_code; }

    /**
     * @brief Short name of the failing subsystem, used by the logger and toasts.
     * @return A static string such as "AluChop", "Database" or "Validation".
     */
    /// @oop-concept Runtime Polymorphism :: one handler prints the right subsystem for any caught child
    virtual const char* category() const noexcept { return "AluChop"; }

protected:
    /**
     * @brief Builds the decorated what() text.
     * @param what    Base message.
     * @param context Offending detail (may be empty).
     * @param code    Numeric code (0 = omit).
     * @return `"message [context] (code N)"`, omitting absent parts.
     */
    static std::string compose(const std::string& what, const std::string& context, int code) {
        std::string full = what;
        if (!context.empty()) {
            full += " [";
            full += context;
            full += "]";
        }
        if (code != 0) {
            full += " (code ";
            full += std::to_string(code);
            full += ")";
        }
        return full;
    }

private:
    std::string m_message; ///< Undecorated message.
    std::string m_full;    ///< Message + context + code; what() returns this.
    std::string m_context; ///< Offending field / path / statement.
    int m_code = 0;        ///< Numeric code, 0 when unused.
};

/**
 * @brief Thrown by the persistence layer when SQLite refuses an operation.
 *
 * Context typically carries the failing statement or table; the code carries
 * the driver's native error number.
 */
class DatabaseException : public AluChopException {
public:
    /**
     * @brief Constructs a database failure from a message.
     * @param what Description of the failure.
     */
    explicit DatabaseException(const std::string& what) : AluChopException("DB: " + what) {}

    /**
     * @brief Constructs a database failure with the offending statement/table and driver code.
     * @param what    Description of the failure.
     * @param context Failing SQL, table or connection name.
     * @param code    Native driver error number; 0 when unknown.
     */
    DatabaseException(const std::string& what, std::string context, int code = 0)
        : AluChopException("DB: " + what, std::move(context), code) {}

    /// @copydoc AluChopException::category
    const char* category() const noexcept override { return "Database"; }
};

/**
 * @brief Thrown by model setters and services when a domain invariant is violated.
 *
 * Context carries the offending field name so the GUI can highlight exactly
 * one input widget.
 */
class ValidationException : public AluChopException {
public:
    /**
     * @brief Constructs a validation failure from a message.
     * @param what Description of the rule that was broken.
     */
    explicit ValidationException(const std::string& what) : AluChopException("Validation: " + what) {}

    /**
     * @brief Constructs a validation failure naming the offending field.
     * @param what    Description of the rule that was broken.
     * @param context Name of the offending field (e.g. "phone").
     * @param code    Optional rule code; 0 when unused.
     */
    ValidationException(const std::string& what, std::string context, int code = 0)
        : AluChopException("Validation: " + what, std::move(context), code) {}

    /// @copydoc AluChopException::category
    const char* category() const noexcept override { return "Validation"; }

    /**
     * @brief Alias for context(), read as "which field was wrong".
     * @return The offending field name; empty when unspecified.
     */
    const std::string& field() const noexcept { return context(); }
};

/**
 * @brief Thrown by AuthService for bad credentials, locked accounts or role denial.
 *
 * Context carries the username or the required role — never the password.
 */
class AuthException : public AluChopException {
public:
    /**
     * @brief Constructs an authentication/authorisation failure.
     * @param what Description of the failure.
     */
    explicit AuthException(const std::string& what) : AluChopException("Auth: " + what) {}

    /**
     * @brief Constructs an authentication failure naming the account or required role.
     * @param what    Description of the failure.
     * @param context Username or required role. Never a secret.
     * @param code    Optional reason code; 0 when unused.
     */
    AuthException(const std::string& what, std::string context, int code = 0)
        : AluChopException("Auth: " + what, std::move(context), code) {}

    /// @copydoc AluChopException::category
    const char* category() const noexcept override { return "Auth"; }
};

/**
 * @brief Thrown by InventoryService when stock cannot satisfy a recipe deduction.
 *
 * Context carries the ingredient that ran out.
 */
class InventoryException : public AluChopException {
public:
    /**
     * @brief Constructs a stock failure.
     * @param what Description of the shortage.
     */
    explicit InventoryException(const std::string& what) : AluChopException("Inventory: " + what) {}

    /**
     * @brief Constructs a stock failure naming the short ingredient.
     * @param what    Description of the shortage.
     * @param context Ingredient name or id.
     * @param code    Optional reason code; 0 when unused.
     */
    InventoryException(const std::string& what, std::string context, int code = 0)
        : AluChopException("Inventory: " + what, std::move(context), code) {}

    /// @copydoc AluChopException::category
    const char* category() const noexcept override { return "Inventory"; }
};

/**
 * @brief Thrown by the raw `<fstream>` layer (Logger, BinaryRecordFile, CsvWriter, BackupManager).
 *
 * Context carries the file path and the failing operation, which is the whole
 * point of SPEC §5.6's "error checking" requirement.
 */
class FileIOException : public AluChopException {
public:
    /**
     * @brief Constructs a file-I/O failure.
     * @param what Description of the failing operation.
     */
    explicit FileIOException(const std::string& what) : AluChopException("FileIO: " + what) {}

    /**
     * @brief Constructs a file-I/O failure naming the path involved.
     * @param what    Description of the failing operation (open/seek/read/write/flush/close).
     * @param context Absolute or relative path of the file.
     * @param code    `errno` at the time of failure, or 0.
     */
    FileIOException(const std::string& what, std::string context, int code = 0)
        : AluChopException("FileIO: " + what, std::move(context), code) {}

    /// @copydoc AluChopException::category
    const char* category() const noexcept override { return "FileIO"; }

    /**
     * @brief Alias for context(), read as "which file failed".
     * @return The file path; empty when unspecified.
     */
    const std::string& path() const noexcept { return context(); }
};

} // namespace aluchop::core

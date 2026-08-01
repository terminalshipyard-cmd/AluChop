#pragma once
/**
 * @file Result.hpp
 * @brief Result<T> — the success-or-error value returned across every service boundary.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * The persistence layer throws (DatabaseException, FileIOException…). The
 * service layer catches those throws once, at its own boundary, and converts
 * them into a Result<T> that the GUI can branch on without ever writing a
 * try/catch inside a widget slot. Exceptions therefore stay for *exceptional*
 * failures, while expected outcomes ("no such promo code") travel as values.
 *
 * Header-only: a class template must be visible in every translation unit
 * that instantiates it.
 */

#include <optional>
#include <stdexcept>
#include <utility>

#include <QString>

namespace aluchop::core {

/**
 * @brief Carries either a value of type @p T or a human-readable error message.
 * @tparam T The success type (a model, a container of models, an int id…).
 *
 * A Result is created only through the ok() / err() factories, which makes an
 * "empty" Result unrepresentable.
 */
/// @oop-concept Class Template :: one generic success-or-error carrier for every service boundary
template <typename T>
class Result {
public:
    /**
     * @brief Builds a successful Result.
     * @param value The produced value; moved into the Result.
     * @return A Result for which isOk() is true.
     */
    static Result ok(T value) {
        Result r;
        r.m_value = std::move(value);
        return r;
    }

    /**
     * @brief Builds a failed Result.
     * @param message User-facing reason for the failure.
     * @return A Result for which isOk() is false.
     */
    static Result err(QString message) {
        Result r;
        r.m_error = std::move(message);
        return r;
    }

    /**
     * @brief Tests for success.
     * @return true when a value is present.
     */
    bool isOk() const noexcept { return m_value.has_value(); }

    /**
     * @brief Tests for failure — the readable inverse of isOk().
     * @return true when no value is present.
     */
    bool isErr() const noexcept { return !m_value.has_value(); }

    /**
     * @brief Allows `if (auto r = service.doThing())` at call sites.
     * @return Same as isOk(); explicit so a Result never decays to a number.
     */
    /// @oop-concept Operator Overloading :: conversion to bool makes the success test read naturally
    explicit operator bool() const noexcept { return isOk(); }

    /**
     * @brief Reads the contained value.
     * @return const reference to the value.
     * @throws std::logic_error when called on a failed Result (a programmer error, not a user error).
     */
    const T& value() const {
        if (!m_value) throw std::logic_error("Result::value() on error Result");
        return *m_value;
    }

    /**
     * @brief Reads or mutates the contained value.
     * @return Mutable reference to the value.
     * @throws std::logic_error when called on a failed Result.
     */
    /// @oop-concept Function Overloading :: const and non-const accessors preserve const-correctness
    T& value() {
        if (!m_value) throw std::logic_error("Result::value() on error Result");
        return *m_value;
    }

    /**
     * @brief Moves the contained value out of the Result.
     * @return The value by move; the Result must not be read again afterwards.
     * @throws std::logic_error when called on a failed Result.
     */
    T take() {
        T v = std::move(value());
        return v;
    }

    /**
     * @brief The failure message.
     * @return const reference to the message; empty on a successful Result.
     */
    const QString& error() const noexcept { return m_error; }

    /**
     * @brief Reads the value, or a caller-supplied fallback when the Result failed.
     * @tparam U Anything convertible/constructible into T.
     * @param fallback Value returned when this Result holds an error.
     * @return The contained value, or T constructed from @p fallback.
     */
    /// @oop-concept Compile-time Polymorphism :: a member template adapts to whatever fallback the caller has
    template <typename U>
    T valueOr(U&& fallback) const {
        return m_value ? *m_value : T(std::forward<U>(fallback));
    }

private:
    /**
     * @brief Private so a Result can only be produced by ok() or err().
     */
    Result() = default;

    std::optional<T> m_value; ///< Engaged exactly when the operation succeeded.
    QString m_error;          ///< Non-empty exactly when the operation failed.
};

/**
 * @brief Full specialisation for operations that succeed without producing a value.
 *
 * `Result<void>` is returned by commands such as "delete this customer" or
 * "restock this ingredient", which either work or explain why they did not.
 */
/// @oop-concept Template Specialisation :: `void` has no storage, so the primary template cannot serve it
template <>
class Result<void> {
public:
    /**
     * @brief Builds a successful Result.
     * @return A Result for which isOk() is true.
     */
    static Result ok() { return Result(true, {}); }

    /**
     * @brief Builds a failed Result.
     * @param message User-facing reason for the failure.
     * @return A Result for which isOk() is false.
     */
    static Result err(QString message) { return Result(false, std::move(message)); }

    /**
     * @brief Tests for success.
     * @return true when the operation completed.
     */
    bool isOk() const noexcept { return m_ok; }

    /**
     * @brief Tests for failure.
     * @return true when the operation failed.
     */
    bool isErr() const noexcept { return !m_ok; }

    /**
     * @brief Allows `if (auto r = service.doThing())` at call sites.
     * @return Same as isOk().
     */
    explicit operator bool() const noexcept { return m_ok; }

    /**
     * @brief The failure message.
     * @return const reference to the message; empty on success.
     */
    const QString& error() const noexcept { return m_error; }

private:
    /**
     * @brief Private state constructor used by ok() and err().
     * @param ok Success flag.
     * @param e  Failure message (empty on success).
     */
    Result(bool ok, QString e) : m_ok(ok), m_error(std::move(e)) {}

    bool m_ok = false; ///< Success flag.
    QString m_error;   ///< Non-empty exactly when the operation failed.
};

} // namespace aluchop::core

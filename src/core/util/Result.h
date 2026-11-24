#pragma once

#include <type_traits>
#include <utility>
#include <variant>

namespace doodlebold {

// C++20 stand-in for std::expected; Result<void, E> carries only the error.
template <typename T, typename E> class [[nodiscard]] Result {
public:
    Result(T value) : m_v(std::in_place_index<0>, std::move(value)) {}  // NOLINT: implicit success
    static Result failure(E error) { return Result(std::in_place_index<1>, std::move(error)); }

    bool ok() const { return m_v.index() == 0; }
    explicit operator bool() const { return ok(); }
    const T& value() const { return std::get<0>(m_v); }
    const E& error() const { return std::get<1>(m_v); }

private:
    template <std::size_t I, typename V> Result(std::in_place_index_t<I> i, V&& v) : m_v(i, std::forward<V>(v)) {}
    std::variant<T, E> m_v;
};

template <typename E> class [[nodiscard]] Result<void, E> {
public:
    Result() = default;
    static Result failure(E error) { return Result(std::move(error), 0); }

    bool ok() const { return !m_failed; }
    explicit operator bool() const { return ok(); }
    const E& error() const { return m_error; }

private:
    Result(E error, int) : m_error(std::move(error)), m_failed(true) {}
    E m_error{};
    bool m_failed = false;
};

}

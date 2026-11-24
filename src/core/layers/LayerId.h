#pragma once

#include <compare>
#include <cstdint>
#include <functional>

namespace doodlebold {

class LayerId {
public:
    constexpr LayerId() = default;
    constexpr explicit LayerId(std::uint32_t value) : m_value(value) {}

    constexpr std::uint32_t value() const { return m_value; }
    constexpr bool isValid() const { return m_value != 0; }
    constexpr explicit operator bool() const { return isValid(); }
    constexpr auto operator<=>(const LayerId&) const = default;

private:
    std::uint32_t m_value = 0;
};

inline constexpr LayerId kNoLayer{};

}

template <> struct std::hash<doodlebold::LayerId> {
    std::size_t operator()(doodlebold::LayerId id) const noexcept { return std::hash<std::uint32_t>{}(id.value()); }
};

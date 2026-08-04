#pragma once

#include <Skein/Foundation/Types.h>

#include <compare>
#include <concepts>
#include <type_traits>

namespace Skein
{
    template<typename Tag, std::unsigned_integral Storage = u64>
    class StrongId final
    {
        static_assert(!std::same_as<Storage, bool>);

    public:
        using ValueType = Storage;

        constexpr StrongId() noexcept = default;

        explicit constexpr StrongId(const Storage value) noexcept
            : m_value(value)
        {
        }

        [[nodiscard]] static constexpr StrongId Invalid() noexcept
        {
            return {};
        }

        [[nodiscard]] constexpr Storage Value() const noexcept
        {
            return m_value;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_value != 0;
        }

        [[nodiscard]] explicit constexpr operator bool() const noexcept
        {
            return IsValid();
        }

        [[nodiscard]] friend constexpr auto operator<=>(
            const StrongId&,
            const StrongId&) noexcept = default;

    private:
        Storage m_value = 0;
    };

    static_assert(std::is_trivially_copyable_v<StrongId<struct StrongIdAbiTag>>);
    static_assert(std::is_standard_layout_v<StrongId<struct StrongIdAbiTag>>);
}

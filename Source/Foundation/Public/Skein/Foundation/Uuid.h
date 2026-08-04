#pragma once

#include <Skein/Foundation/String.h>

#include <array>
#include <compare>

namespace Skein
{
    using UuidText = std::array<char, 37>;

    class Uuid final
    {
    public:
        using Bytes = std::array<u8, 16>;

        constexpr Uuid() noexcept = default;

        explicit constexpr Uuid(const Bytes bytes) noexcept
            : m_bytes(bytes)
        {
        }

        [[nodiscard]] constexpr const Bytes& ByteValues() const noexcept
        {
            return m_bytes;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            for (const u8 value : m_bytes)
            {
                if (value != 0)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] static Result<Uuid> Parse(StringView text) noexcept;
        [[nodiscard]] UuidText ToString() const noexcept;

        [[nodiscard]] friend constexpr auto operator<=>(
            const Uuid&,
            const Uuid&) noexcept = default;

    private:
        Bytes m_bytes{};
    };
}

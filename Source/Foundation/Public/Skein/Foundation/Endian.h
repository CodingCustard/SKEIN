#pragma once

#include <Skein/Foundation/Types.h>

#include <bit>
#include <concepts>
#include <type_traits>

namespace Skein
{
    enum class Endian : u8
    {
        Little,
        Big
    };

    static_assert(
        std::endian::native == std::endian::little ||
        std::endian::native == std::endian::big);

    inline constexpr Endian NativeEndian =
        std::endian::native == std::endian::little ? Endian::Little : Endian::Big;

    template<std::unsigned_integral T>
        requires (!std::same_as<T, bool>)
    [[nodiscard]] constexpr T ByteSwap(const T value) noexcept
    {
        if constexpr (sizeof(T) == 1)
        {
            return value;
        }
        else if constexpr (sizeof(T) == 2)
        {
            return static_cast<T>((value << 8U) | (value >> 8U));
        }
        else if constexpr (sizeof(T) == 4)
        {
            return static_cast<T>(
                ((value & static_cast<T>(0x000000FFU)) << 24U) |
                ((value & static_cast<T>(0x0000FF00U)) << 8U) |
                ((value & static_cast<T>(0x00FF0000U)) >> 8U) |
                ((value & static_cast<T>(0xFF000000U)) >> 24U));
        }
        else if constexpr (sizeof(T) == 8)
        {
            return static_cast<T>(
                ((value & static_cast<T>(0x00000000000000FFULL)) << 56U) |
                ((value & static_cast<T>(0x000000000000FF00ULL)) << 40U) |
                ((value & static_cast<T>(0x0000000000FF0000ULL)) << 24U) |
                ((value & static_cast<T>(0x00000000FF000000ULL)) << 8U) |
                ((value & static_cast<T>(0x000000FF00000000ULL)) >> 8U) |
                ((value & static_cast<T>(0x0000FF0000000000ULL)) >> 24U) |
                ((value & static_cast<T>(0x00FF000000000000ULL)) >> 40U) |
                ((value & static_cast<T>(0xFF00000000000000ULL)) >> 56U));
        }
        else
        {
            static_assert(sizeof(T) <= 8, "ByteSwap supports 8, 16, 32 and 64-bit values.");
        }
    }

    template<std::unsigned_integral T>
        requires (!std::same_as<T, bool>)
    [[nodiscard]] constexpr T ConvertEndian(
        const T value,
        const Endian source,
        const Endian destination) noexcept
    {
        return source == destination ? value : ByteSwap(value);
    }
}

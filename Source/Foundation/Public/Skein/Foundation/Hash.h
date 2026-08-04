#pragma once

#include <Skein/Foundation/String.h>

namespace Skein
{
    inline constexpr u64 StableHashOffset64 = 14695981039346656037ULL;
    inline constexpr u64 StableHashPrime64 = 1099511628211ULL;

    [[nodiscard]] constexpr u64 StableHash64(const StringView text) noexcept
    {
        u64 hash = StableHashOffset64;
        for (const char character : text)
        {
            hash ^= static_cast<u8>(character);
            hash *= StableHashPrime64;
        }

        return hash == 0 ? 1 : hash;
    }

    [[nodiscard]] constexpr u64 StableHash64(const ConstByteSpan bytes) noexcept
    {
        u64 hash = StableHashOffset64;
        for (const Byte value : bytes)
        {
            hash ^= std::to_integer<u8>(value);
            hash *= StableHashPrime64;
        }

        return hash == 0 ? 1 : hash;
    }
}

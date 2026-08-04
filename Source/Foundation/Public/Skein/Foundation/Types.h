#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace Skein
{
    using i8  = std::int8_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;

    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;

    using f32 = float;
    using f64 = double;

    using Byte = std::byte;
    using usize = std::size_t;

    template<typename T, std::size_t Extent = std::dynamic_extent>
    using Span = std::span<T, Extent>;

    using ByteSpan = Span<Byte>;
    using ConstByteSpan = Span<const Byte>;

    using Nanoseconds = std::chrono::nanoseconds;
    using Microseconds = std::chrono::microseconds;
    using Milliseconds = std::chrono::milliseconds;
    using Seconds = std::chrono::seconds;
    using SecondsF = std::chrono::duration<f32>;
    using SecondsD = std::chrono::duration<f64>;

    static_assert(sizeof(i8) == 1);
    static_assert(sizeof(i16) == 2);
    static_assert(sizeof(i32) == 4);
    static_assert(sizeof(i64) == 8);
    static_assert(sizeof(u8) == 1);
    static_assert(sizeof(u16) == 2);
    static_assert(sizeof(u32) == 4);
    static_assert(sizeof(u64) == 8);
    static_assert(sizeof(f32) == 4 && std::numeric_limits<f32>::is_iec559);
    static_assert(sizeof(f64) == 8 && std::numeric_limits<f64>::is_iec559);
}

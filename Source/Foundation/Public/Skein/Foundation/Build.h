#pragma once

#include <Skein/Foundation/BuildConfig.h>

#include <cstdint>
#include <string_view>

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

    enum class BuildMode : u8
    {
        Debug,
        Release
    };

    struct RuntimeFeatures final
    {
        bool Assertions = false;
        bool Tracing = false;

        [[nodiscard]] friend constexpr bool operator==(
            const RuntimeFeatures&,
            const RuntimeFeatures&) noexcept = default;
    };

    struct BuildMetadata final
    {
        std::string_view EngineName;
        std::string_view EngineVersion;
        std::string_view CompilerName;
        std::string_view CompilerVersion;
        BuildMode Mode;
        RuntimeFeatures CompiledFeatures;
    };

    inline constexpr BuildMetadata CurrentBuild
    {
        SKEIN_ENGINE_NAME,
        SKEIN_ENGINE_VERSION,
        SKEIN_COMPILER_NAME,
        SKEIN_COMPILER_VERSION,
#if SKEIN_BUILD_DEBUG
        BuildMode::Debug,
#else
        BuildMode::Release,
#endif
        {
            SKEIN_ENABLE_ASSERTS != 0,
            SKEIN_ENABLE_TRACING != 0
        }
    };

    [[nodiscard]] constexpr RuntimeFeatures ResolveRuntimeFeatures(
        const RuntimeFeatures requested) noexcept
    {
        return {
            requested.Assertions && CurrentBuild.CompiledFeatures.Assertions,
            requested.Tracing && CurrentBuild.CompiledFeatures.Tracing
        };
    }
}

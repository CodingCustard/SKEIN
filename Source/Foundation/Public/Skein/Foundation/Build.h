#pragma once

#include <Skein/Foundation/BuildConfig.h>
#include <Skein/Foundation/Types.h>

#include <string_view>

namespace Skein
{
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

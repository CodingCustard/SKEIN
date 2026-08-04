#include <Skein/Foundation/Build.h>

#include <string_view>

namespace
{
    static_assert(SKEIN_ENGINE_VERSION_MAJOR == 0);
    static_assert(SKEIN_ENGINE_VERSION_MINOR == 1);
    static_assert(SKEIN_ENGINE_VERSION_PATCH == 0);
    static_assert(SKEIN_BUILD_DEBUG != SKEIN_BUILD_RELEASE);
}

int main()
{
    using namespace Skein;

    if (CurrentBuild.EngineName != std::string_view{"SKEIN"} ||
        CurrentBuild.EngineVersion != std::string_view{"0.1.0"} ||
        CurrentBuild.CompilerName.empty() ||
        CurrentBuild.CompilerVersion.empty())
    {
        return 1;
    }

    const RuntimeFeatures disabled = ResolveRuntimeFeatures({false, false});
    if (disabled != RuntimeFeatures{})
    {
        return 2;
    }

    const RuntimeFeatures requested = ResolveRuntimeFeatures({true, true});
    if (requested != CurrentBuild.CompiledFeatures)
    {
        return 3;
    }

#if SKEIN_BUILD_DEBUG
    if (CurrentBuild.Mode != BuildMode::Debug ||
        !CurrentBuild.CompiledFeatures.Assertions ||
        !CurrentBuild.CompiledFeatures.Tracing)
    {
        return 4;
    }
#else
    if (CurrentBuild.Mode != BuildMode::Release ||
        CurrentBuild.CompiledFeatures.Assertions ||
        CurrentBuild.CompiledFeatures.Tracing)
    {
        return 5;
    }
#endif

    return 0;
}

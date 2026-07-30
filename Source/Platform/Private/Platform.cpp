#include <Skein/Platform/Platform.h>

namespace Skein
{
    std::string_view Platform::Name() noexcept
    {
#if defined(SKEIN_PLATFORM_WINDOWS)
        return "Windows x64";
#else
        return "Unknown";
#endif
    }
}

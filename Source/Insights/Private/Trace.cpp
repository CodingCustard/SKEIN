#include <Skein/Insights/Trace.h>

#include <Skein/Foundation/Build.h>
#include <Skein/Foundation/Log.h>

namespace Skein::Trace
{
    void Bookmark(const std::string_view category, const std::string_view message)
    {
#if SKEIN_ENABLE_TRACING
        Log(LogLevel::Trace, category, message);
#else
        (void)category;
        (void)message;
#endif
    }
}

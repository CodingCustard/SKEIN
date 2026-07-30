#include <Skein/UI/UI.h>

#include <Skein/Foundation/Log.h>

namespace Skein::UI
{
    void Initialise()
    {
        Log(LogLevel::Info, "SkeinUI", "Initialising UI framework");
    }

    void Shutdown()
    {
        Log(LogLevel::Info, "SkeinUI", "Shutting down UI framework");
    }
}

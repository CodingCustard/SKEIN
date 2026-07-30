#include <Skein/Core/Engine.h>
#include <Skein/Foundation/Log.h>
#include <Skein/Insights/Trace.h>

int main()
{
    Skein::Engine engine;

    if (!engine.Initialise())
    {
        Skein::Log(Skein::LogLevel::Fatal, "Runtime", "Engine initialisation failed");
        return 1;
    }

    Skein::Trace::Bookmark("Runtime", "First engine frame");
    engine.Tick();
    engine.Shutdown();
    return 0;
}

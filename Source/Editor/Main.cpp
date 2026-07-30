#include <Skein/Core/Engine.h>
#include <Skein/Foundation/Log.h>
#include <Skein/Insights/Trace.h>
#include <Skein/UI/UI.h>

int main()
{
    Skein::Engine engine;

    if (!engine.Initialise())
    {
        Skein::Log(Skein::LogLevel::Fatal, "Editor", "Engine initialisation failed");
        return 1;
    }

    Skein::UI::Initialise();
    Skein::Trace::Bookmark("Editor", "SkeinEditor started");
    Skein::Log(Skein::LogLevel::Info, "Editor", "SkeinEditor skeleton is running");

    Skein::UI::Shutdown();
    engine.Shutdown();
    return 0;
}

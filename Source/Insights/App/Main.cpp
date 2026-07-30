#include <Skein/Foundation/Log.h>
#include <Skein/Insights/Trace.h>
#include <Skein/UI/UI.h>

int main()
{
    Skein::UI::Initialise();
    Skein::Trace::Bookmark("Insights", "SkeinInsights started");
    Skein::Log(Skein::LogLevel::Info, "Insights", "SkeinInsights skeleton is running");
    Skein::UI::Shutdown();
    return 0;
}

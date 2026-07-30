#include <Skein/Core/Engine.h>

#include <Skein/Foundation/Log.h>
#include <Skein/Platform/Platform.h>

#include <string>

namespace Skein
{
    bool Engine::Initialise()
    {
        Log(LogLevel::Info, "Core", std::string("Initialising SKEIN on ") + std::string(Platform::Name()));
        m_running = true;
        return true;
    }

    void Engine::Tick()
    {
        if (!m_running)
        {
            return;
        }
    }

    void Engine::Shutdown()
    {
        if (!m_running)
        {
            return;
        }

        Log(LogLevel::Info, "Core", "Shutting down SKEIN");
        m_running = false;
    }
}

#include <Skein/Core/Engine.h>

#include <Skein/Foundation/Log.h>
#include <Skein/Platform/Platform.h>

namespace Skein
{
    Result<void> Engine::Initialise()
    {
        String message = "Initialising SKEIN on ";
        message.append(Platform::Name());
        Log(LogLevel::Info, "Core", message);
        m_running = true;
        return {};
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

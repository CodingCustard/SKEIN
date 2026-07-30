#pragma once

namespace Skein
{
    class Engine final
    {
    public:
        bool Initialise();
        void Tick();
        void Shutdown();

    private:
        bool m_running = false;
    };
}

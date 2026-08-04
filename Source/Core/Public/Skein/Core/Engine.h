#pragma once

#include <Skein/Foundation/Result.h>

namespace Skein
{
    class Engine final
    {
    public:
        [[nodiscard]] Result<void> Initialise();
        void Tick();
        void Shutdown();

    private:
        bool m_running = false;
    };
}

#include <Skein/Core/Engine.h>

int main()
{
    Skein::Engine engine;

    if (!engine.Initialise())
    {
        return 1;
    }

    engine.Tick();
    engine.Shutdown();
    return 0;
}

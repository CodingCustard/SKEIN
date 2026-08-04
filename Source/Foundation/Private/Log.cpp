#include <Skein/Foundation/Log.h>

#include <iostream>

namespace Skein
{
    void Log(const LogLevel level, const StringView category, const StringView message)
    {
        const char* levelName = "Unknown";

        switch (level)
        {
        case LogLevel::Trace:   levelName = "Trace"; break;
        case LogLevel::Info:    levelName = "Info"; break;
        case LogLevel::Warning: levelName = "Warning"; break;
        case LogLevel::Error:   levelName = "Error"; break;
        case LogLevel::Fatal:   levelName = "Fatal"; break;
        }

        std::cout << '[' << levelName << "][" << category << "] " << message << '\n';
    }
}

#pragma once

#include <string_view>

namespace Skein
{
    enum class LogLevel
    {
        Trace,
        Info,
        Warning,
        Error,
        Fatal
    };

    void Log(LogLevel level, std::string_view category, std::string_view message);
}

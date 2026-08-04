#pragma once

#include <Skein/Foundation/String.h>

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

    void Log(LogLevel level, StringView category, StringView message);
}

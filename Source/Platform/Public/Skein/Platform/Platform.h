#pragma once

#include <string_view>

namespace Skein
{
    class Platform final
    {
    public:
        [[nodiscard]] static std::string_view Name() noexcept;
    };
}

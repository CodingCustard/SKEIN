#pragma once

#include <Skein/Foundation/Result.h>

#include <string>
#include <string_view>

namespace Skein
{
    // All narrow strings crossing SKEIN public interfaces contain UTF-8.
    using String = std::string;
    using StringView = std::string_view;

    [[nodiscard]] bool IsValidUtf8(StringView text) noexcept;
    [[nodiscard]] Result<void> ValidateUtf8(StringView text) noexcept;
}

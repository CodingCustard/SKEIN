#pragma once

#include <string_view>

namespace Skein::Trace
{
    void Bookmark(std::string_view category, std::string_view message);
}

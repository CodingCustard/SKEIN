#pragma once

#include <Skein/Foundation/String.h>

namespace Skein
{
    class NormalizedPath final
    {
    public:
        constexpr NormalizedPath() noexcept = default;

        [[nodiscard]] StringView View() const noexcept
        {
            return m_value;
        }

        [[nodiscard]] const String& StringValue() const noexcept
        {
            return m_value;
        }

        [[nodiscard]] bool IsAbsolute() const noexcept
        {
            return m_absolute;
        }

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return m_value.empty();
        }

        [[nodiscard]] friend bool operator==(
            const NormalizedPath&,
            const NormalizedPath&) noexcept = default;

    private:
        friend Result<NormalizedPath> NormalizePath(StringView);

        explicit NormalizedPath(String value, const bool absolute) noexcept
            : m_value(std::move(value)),
              m_absolute(absolute)
        {
        }

        String m_value;
        bool m_absolute = false;
    };

    [[nodiscard]] Result<NormalizedPath> NormalizePath(StringView path);
}

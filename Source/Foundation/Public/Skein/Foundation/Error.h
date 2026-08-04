#pragma once

#include <Skein/Foundation/Types.h>

#include <algorithm>
#include <array>
#include <source_location>
#include <string_view>

namespace Skein
{
    enum class ErrorCode : u16
    {
        None = 0,
        InvalidArgument,
        InvalidState,
        NotFound,
        AlreadyExists,
        OutOfMemory,
        Unsupported,
        InputOutput,
        Timeout,
        Cancelled,
        Internal
    };

    class Error final
    {
    public:
        static constexpr std::size_t MaximumMessageLength = 191;

        constexpr Error() noexcept = default;

        constexpr Error(
            const ErrorCode code,
            const std::string_view message,
            const std::source_location source = std::source_location::current()) noexcept
            : m_code(code),
              m_source(source)
        {
            const std::size_t length = std::min(message.size(), MaximumMessageLength);
            std::copy_n(message.data(), length, m_message.data());
            m_message[length] = '\0';
            m_messageLength = static_cast<u16>(length);
        }

        [[nodiscard]] constexpr ErrorCode Code() const noexcept
        {
            return m_code;
        }

        [[nodiscard]] constexpr std::string_view Message() const noexcept
        {
            return {m_message.data(), m_messageLength};
        }

        [[nodiscard]] constexpr const std::source_location& Source() const noexcept
        {
            return m_source;
        }

        [[nodiscard]] constexpr bool IsFailure() const noexcept
        {
            return m_code != ErrorCode::None;
        }

        [[nodiscard]] explicit constexpr operator bool() const noexcept
        {
            return IsFailure();
        }

    private:
        ErrorCode m_code = ErrorCode::None;
        u16 m_messageLength = 0;
        std::array<char, MaximumMessageLength + 1> m_message{};
        std::source_location m_source{};
    };
}

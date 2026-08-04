#include <Skein/Foundation/String.h>

namespace Skein
{
    namespace
    {
        [[nodiscard]] constexpr bool IsContinuation(const u8 value) noexcept
        {
            return (value & 0xC0U) == 0x80U;
        }
    }

    bool IsValidUtf8(const StringView text) noexcept
    {
        std::size_t index = 0;
        while (index < text.size())
        {
            const u8 first = static_cast<u8>(text[index]);
            if (first <= 0x7FU)
            {
                ++index;
                continue;
            }

            if (first >= 0xC2U && first <= 0xDFU)
            {
                if (index + 1 >= text.size() ||
                    !IsContinuation(static_cast<u8>(text[index + 1])))
                {
                    return false;
                }
                index += 2;
                continue;
            }

            if (first >= 0xE0U && first <= 0xEFU)
            {
                if (index + 2 >= text.size())
                {
                    return false;
                }

                const u8 second = static_cast<u8>(text[index + 1]);
                const u8 third = static_cast<u8>(text[index + 2]);
                const bool validSecond =
                    (first == 0xE0U && second >= 0xA0U && second <= 0xBFU) ||
                    (first == 0xEDU && second >= 0x80U && second <= 0x9FU) ||
                    ((first != 0xE0U && first != 0xEDU) && IsContinuation(second));
                if (!validSecond || !IsContinuation(third))
                {
                    return false;
                }
                index += 3;
                continue;
            }

            if (first >= 0xF0U && first <= 0xF4U)
            {
                if (index + 3 >= text.size())
                {
                    return false;
                }

                const u8 second = static_cast<u8>(text[index + 1]);
                const bool validSecond =
                    (first == 0xF0U && second >= 0x90U && second <= 0xBFU) ||
                    (first == 0xF4U && second >= 0x80U && second <= 0x8FU) ||
                    ((first > 0xF0U && first < 0xF4U) && IsContinuation(second));
                if (!validSecond ||
                    !IsContinuation(static_cast<u8>(text[index + 2])) ||
                    !IsContinuation(static_cast<u8>(text[index + 3])))
                {
                    return false;
                }
                index += 4;
                continue;
            }

            return false;
        }

        return true;
    }

    Result<void> ValidateUtf8(const StringView text) noexcept
    {
        if (!IsValidUtf8(text))
        {
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid UTF-8 sequence"}};
        }

        return {};
    }
}

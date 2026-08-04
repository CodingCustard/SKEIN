#include <Skein/Foundation/Uuid.h>

namespace Skein
{
    namespace
    {
        [[nodiscard]] constexpr i32 HexValue(const char character) noexcept
        {
            if (character >= '0' && character <= '9')
            {
                return character - '0';
            }
            if (character >= 'a' && character <= 'f')
            {
                return character - 'a' + 10;
            }
            if (character >= 'A' && character <= 'F')
            {
                return character - 'A' + 10;
            }
            return -1;
        }
    }

    Result<Uuid> Uuid::Parse(const StringView text) noexcept
    {
        if (text.size() != 36 || text[8] != '-' || text[13] != '-' ||
            text[18] != '-' || text[23] != '-')
        {
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid UUID format"}};
        }

        Bytes bytes{};
        std::size_t byteIndex = 0;
        for (std::size_t index = 0; index < text.size();)
        {
            if (text[index] == '-')
            {
                if (index != 8 && index != 13 && index != 18 && index != 23)
                {
                    return Unexpected{Error{ErrorCode::InvalidArgument, "invalid UUID separator"}};
                }
                ++index;
                continue;
            }

            const i32 high = HexValue(text[index]);
            const i32 low = HexValue(text[index + 1]);
            if (high < 0 || low < 0)
            {
                return Unexpected{Error{ErrorCode::InvalidArgument, "invalid UUID digit"}};
            }

            bytes[byteIndex++] = static_cast<u8>((high << 4) | low);
            index += 2;
        }

        if (byteIndex != bytes.size())
        {
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid UUID length"}};
        }

        return Uuid{bytes};
    }

    UuidText Uuid::ToString() const noexcept
    {
        constexpr char digits[] = "0123456789abcdef";
        UuidText result{};
        std::size_t output = 0;
        for (std::size_t index = 0; index < m_bytes.size(); ++index)
        {
            if (index == 4 || index == 6 || index == 8 || index == 10)
            {
                result[output++] = '-';
            }
            result[output++] = digits[m_bytes[index] >> 4U];
            result[output++] = digits[m_bytes[index] & 0x0FU];
        }
        result[output] = '\0';
        return result;
    }
}

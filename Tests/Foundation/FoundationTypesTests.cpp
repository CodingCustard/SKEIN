#include <Skein/Foundation/Endian.h>
#include <Skein/Foundation/StrongId.h>
#include <Skein/Foundation/Types.h>

#include <array>
#include <chrono>
#include <type_traits>

namespace
{
    struct TextureTag;
    struct BufferTag;

    using TextureId = Skein::StrongId<TextureTag>;
    using BufferId = Skein::StrongId<BufferTag>;

    static_assert(!std::is_convertible_v<Skein::u64, TextureId>);
    static_assert(!std::is_convertible_v<TextureId, Skein::u64>);
    static_assert(!std::is_same_v<TextureId, BufferId>);
    static_assert(sizeof(TextureId) == sizeof(Skein::u64));
    static_assert(Skein::ByteSwap<Skein::u16>(0x1234U) == 0x3412U);
    static_assert(Skein::ByteSwap<Skein::u32>(0x12345678U) == 0x78563412U);
    static_assert(
        Skein::ByteSwap<Skein::u64>(0x0123456789ABCDEFULL) ==
        0xEFCDAB8967452301ULL);
}

int main()
{
    using namespace Skein;

    const TextureId invalid;
    const TextureId texture{42};
    const TextureId sameTexture{42};
    const TextureId otherTexture{7};

    if (invalid.IsValid() || invalid != TextureId::Invalid())
    {
        return 1;
    }

    if (!texture.IsValid() || texture.Value() != 42 ||
        texture != sameTexture || texture == otherTexture)
    {
        return 2;
    }

    std::array<Byte, 4> bytes{};
    ByteSpan writable{bytes};
    ConstByteSpan readable{writable};
    writable[0] = static_cast<Byte>(0x2A);

    if (readable.size_bytes() != 4 || std::to_integer<u8>(readable[0]) != 0x2A)
    {
        return 3;
    }

    if (ConvertEndian<u32>(0x01020304U, Endian::Little, Endian::Big) !=
        0x04030201U)
    {
        return 4;
    }

    constexpr Milliseconds frameMilliseconds{16};
    constexpr SecondsD frameSeconds = frameMilliseconds;
    if (frameSeconds.count() != 0.016)
    {
        return 5;
    }

    return 0;
}

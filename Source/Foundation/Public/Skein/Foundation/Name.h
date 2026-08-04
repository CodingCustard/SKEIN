#pragma once

#include <Skein/Foundation/FlatMap.h>
#include <Skein/Foundation/Hash.h>

#include <compare>
#include <mutex>

namespace Skein
{
    class Name final
    {
    public:
        constexpr Name() noexcept = default;

        [[nodiscard]] constexpr u64 Value() const noexcept
        {
            return m_value;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_value != 0;
        }

        [[nodiscard]] friend constexpr auto operator<=>(
            const Name&,
            const Name&) noexcept = default;

    private:
        friend class NameTable;

        explicit constexpr Name(const u64 value) noexcept
            : m_value(value)
        {
        }

        u64 m_value = 0;
    };

    class NameTable final
    {
    public:
        [[nodiscard]] Result<Name> Intern(StringView text);
        [[nodiscard]] Result<String> Resolve(Name name) const;
        [[nodiscard]] std::size_t Size() const;
        void Clear();

    private:
        mutable std::mutex m_mutex;
        FlatMap<u64, String> m_names;
    };
}

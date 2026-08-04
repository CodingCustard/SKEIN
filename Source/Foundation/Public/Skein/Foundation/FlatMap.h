#pragma once

#include <Skein/Foundation/Memory.h>

#include <algorithm>
#include <functional>
#include <new>
#include <utility>
#include <vector>

namespace Skein
{
    template<
        typename Key,
        typename Value,
        typename Compare = std::less<Key>,
        typename Allocator = SkeinAllocator<std::pair<Key, Value>>>
    class FlatMap final
    {
    public:
        using Entry = std::pair<Key, Value>;

        FlatMap() = default;

        explicit FlatMap(const Allocator& allocator)
            : m_entries(allocator)
        {
        }

        FlatMap(const Compare& compare, const Allocator& allocator)
            : m_entries(allocator),
              m_compare(compare)
        {
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_entries.size(); }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_entries.empty(); }

        [[nodiscard]] Value* Find(const Key& key)
        {
            const auto iterator = LowerBound(key);
            return iterator != m_entries.end() && KeysEqual(iterator->first, key)
                ? &iterator->second
                : nullptr;
        }

        [[nodiscard]] const Value* Find(const Key& key) const
        {
            const auto iterator = LowerBound(key);
            return iterator != m_entries.end() && KeysEqual(iterator->first, key)
                ? &iterator->second
                : nullptr;
        }

        // Insertion may invalidate all pointers returned by Find.
        [[nodiscard]] Result<bool> InsertOrAssign(Key key, Value value)
        {
            try
            {
                const auto iterator = LowerBound(key);
                if (iterator != m_entries.end() && KeysEqual(iterator->first, key))
                {
                    iterator->second = std::move(value);
                    return false;
                }
                m_entries.insert(iterator, Entry{std::move(key), std::move(value)});
                return true;
            }
            catch (const std::bad_alloc&)
            {
                return Unexpected{Error{ErrorCode::OutOfMemory, "FlatMap allocation failed"}};
            }
            catch (...)
            {
                return Unexpected{Error{ErrorCode::Internal, "FlatMap insertion failed"}};
            }
        }

        [[nodiscard]] bool Erase(const Key& key)
        {
            const auto iterator = LowerBound(key);
            if (iterator == m_entries.end() || !KeysEqual(iterator->first, key))
            {
                return false;
            }
            m_entries.erase(iterator);
            return true;
        }

        void Clear() noexcept { m_entries.clear(); }

    private:
        [[nodiscard]] auto LowerBound(const Key& key)
        {
            return std::lower_bound(
                m_entries.begin(),
                m_entries.end(),
                key,
                [this](const Entry& entry, const Key& candidate)
                {
                    return m_compare(entry.first, candidate);
                });
        }

        [[nodiscard]] auto LowerBound(const Key& key) const
        {
            return std::lower_bound(
                m_entries.begin(),
                m_entries.end(),
                key,
                [this](const Entry& entry, const Key& candidate)
                {
                    return m_compare(entry.first, candidate);
                });
        }

        [[nodiscard]] bool KeysEqual(const Key& left, const Key& right) const
        {
            return !m_compare(left, right) && !m_compare(right, left);
        }

        std::vector<Entry, Allocator> m_entries;
        Compare m_compare;
    };
}

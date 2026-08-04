#pragma once

#include <Skein/Foundation/Result.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace Skein
{
    template<typename T, std::size_t InlineCapacity>
    class SmallVector final
    {
        static_assert(InlineCapacity > 0);

    public:
        SmallVector() noexcept
            : m_data(InlineData())
        {
        }

        SmallVector(const SmallVector&) = delete;
        SmallVector& operator=(const SmallVector&) = delete;

        SmallVector(SmallVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
            : m_data(InlineData())
        {
            MoveFrom(std::move(other));
        }

        SmallVector& operator=(SmallVector&& other) noexcept(
            std::is_nothrow_move_constructible_v<T>)
        {
            if (this != &other)
            {
                ReleaseStorage();
                m_data = InlineData();
                m_capacity = InlineCapacity;
                MoveFrom(std::move(other));
            }
            return *this;
        }

        ~SmallVector()
        {
            ReleaseStorage();
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_size; }
        [[nodiscard]] std::size_t Capacity() const noexcept { return m_capacity; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_size == 0; }
        [[nodiscard]] bool IsInline() const noexcept { return m_data == InlineData(); }

        [[nodiscard]] T* Data() noexcept { return m_data; }
        [[nodiscard]] const T* Data() const noexcept { return m_data; }
        [[nodiscard]] T* begin() noexcept { return m_data; }
        [[nodiscard]] const T* begin() const noexcept { return m_data; }
        [[nodiscard]] T* end() noexcept { return m_data + m_size; }
        [[nodiscard]] const T* end() const noexcept { return m_data + m_size; }

        [[nodiscard]] T& operator[](const std::size_t index) noexcept
        {
            (void)SKEIN_VERIFY_MESSAGE(index < m_size, "SmallVector index is out of range");
            return m_data[index];
        }

        [[nodiscard]] const T& operator[](const std::size_t index) const noexcept
        {
            (void)SKEIN_VERIFY_MESSAGE(index < m_size, "SmallVector index is out of range");
            return m_data[index];
        }

        [[nodiscard]] T& Back() noexcept
        {
            (void)SKEIN_VERIFY_MESSAGE(m_size != 0, "SmallVector is empty");
            return m_data[m_size - 1];
        }

        [[nodiscard]] const T& Back() const noexcept
        {
            (void)SKEIN_VERIFY_MESSAGE(m_size != 0, "SmallVector is empty");
            return m_data[m_size - 1];
        }

        template<typename... Arguments>
        [[nodiscard]] Result<T*> EmplaceBack(Arguments&&... arguments)
        {
            if (m_size == m_capacity)
            {
                const std::size_t nextCapacity = NextCapacity();
                if (nextCapacity <= m_capacity)
                {
                    return Unexpected{Error{
                        ErrorCode::OutOfMemory,
                        "SmallVector capacity exhausted"}};
                }
                SKEIN_TRY(Reserve(nextCapacity));
            }

            try
            {
                std::allocator_traits<Allocator>::construct(
                    m_allocator,
                    m_data + m_size,
                    std::forward<Arguments>(arguments)...);
                T* const value = m_data + m_size;
                ++m_size;
                return value;
            }
            catch (const std::bad_alloc&)
            {
                return Unexpected{Error{ErrorCode::OutOfMemory, "SmallVector element allocation failed"}};
            }
            catch (...)
            {
                return Unexpected{Error{ErrorCode::Internal, "SmallVector element construction failed"}};
            }
        }

        [[nodiscard]] Result<void> PushBack(const T& value)
            requires std::copy_constructible<T>
        {
            Result<T*> result = EmplaceBack(value);
            if (!result)
            {
                return Unexpected{result.ErrorValue()};
            }
            return {};
        }

        [[nodiscard]] Result<void> PushBack(T&& value)
        {
            Result<T*> result = EmplaceBack(std::move(value));
            if (!result)
            {
                return Unexpected{result.ErrorValue()};
            }
            return {};
        }

        [[nodiscard]] Result<void> Reserve(const std::size_t requestedCapacity)
        {
            if (requestedCapacity <= m_capacity)
            {
                return {};
            }
            if (requestedCapacity > std::allocator_traits<Allocator>::max_size(m_allocator))
            {
                return Unexpected{Error{ErrorCode::OutOfMemory, "SmallVector capacity is too large"}};
            }

            T* replacement = nullptr;
            try
            {
                replacement = std::allocator_traits<Allocator>::allocate(
                    m_allocator,
                    requestedCapacity);

                std::size_t constructed = 0;
                try
                {
                    for (; constructed < m_size; ++constructed)
                    {
                        std::allocator_traits<Allocator>::construct(
                            m_allocator,
                            replacement + constructed,
                            std::move_if_noexcept(m_data[constructed]));
                    }
                }
                catch (...)
                {
                    std::destroy_n(replacement, constructed);
                    std::allocator_traits<Allocator>::deallocate(
                        m_allocator,
                        replacement,
                        requestedCapacity);
                    throw;
                }
            }
            catch (const std::bad_alloc&)
            {
                return Unexpected{Error{ErrorCode::OutOfMemory, "SmallVector growth allocation failed"}};
            }
            catch (...)
            {
                return Unexpected{Error{ErrorCode::Internal, "SmallVector relocation failed"}};
            }

            std::destroy_n(m_data, m_size);
            if (!IsInline())
            {
                std::allocator_traits<Allocator>::deallocate(m_allocator, m_data, m_capacity);
            }
            m_data = replacement;
            m_capacity = requestedCapacity;
            return {};
        }

        [[nodiscard]] bool PopBack() noexcept
        {
            if (m_size == 0)
            {
                return false;
            }
            --m_size;
            std::allocator_traits<Allocator>::destroy(m_allocator, m_data + m_size);
            return true;
        }

        void Clear() noexcept
        {
            std::destroy_n(m_data, m_size);
            m_size = 0;
        }

    private:
        using Allocator = std::allocator<T>;

        [[nodiscard]] T* InlineData() noexcept
        {
            return std::launder(reinterpret_cast<T*>(m_inlineStorage));
        }

        [[nodiscard]] const T* InlineData() const noexcept
        {
            return std::launder(reinterpret_cast<const T*>(m_inlineStorage));
        }

        [[nodiscard]] std::size_t NextCapacity() const noexcept
        {
            if (m_capacity > std::numeric_limits<std::size_t>::max() / 2)
            {
                return std::numeric_limits<std::size_t>::max();
            }
            return m_capacity * 2;
        }

        void ReleaseStorage() noexcept
        {
            Clear();
            if (!IsInline())
            {
                std::allocator_traits<Allocator>::deallocate(m_allocator, m_data, m_capacity);
            }
        }

        void MoveFrom(SmallVector&& other)
        {
            if (!other.IsInline())
            {
                m_data = other.m_data;
                m_size = other.m_size;
                m_capacity = other.m_capacity;
                other.m_data = other.InlineData();
                other.m_size = 0;
                other.m_capacity = InlineCapacity;
                return;
            }

            for (T& value : other)
            {
                std::allocator_traits<Allocator>::construct(
                    m_allocator,
                    m_data + m_size,
                    std::move(value));
                ++m_size;
            }
            other.Clear();
        }

        alignas(T) Byte m_inlineStorage[sizeof(T) * InlineCapacity]{};
        Allocator m_allocator;
        T* m_data;
        std::size_t m_size = 0;
        std::size_t m_capacity = InlineCapacity;
    };
}

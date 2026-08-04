#pragma once

#include <Skein/Foundation/Result.h>

#include <array>
#include <new>
#include <optional>
#include <utility>

namespace Skein
{
    template<typename T, std::size_t CapacityValue>
    class RingBuffer final
    {
        static_assert(CapacityValue > 0);

    public:
        [[nodiscard]] static constexpr std::size_t Capacity() noexcept
        {
            return CapacityValue;
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_size; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_size == 0; }
        [[nodiscard]] bool IsFull() const noexcept { return m_size == CapacityValue; }

        template<typename... Arguments>
        [[nodiscard]] Result<void> Emplace(Arguments&&... arguments)
        {
            if (IsFull())
            {
                return Unexpected{Error{ErrorCode::InvalidState, "RingBuffer is full"}};
            }
            try
            {
                m_values[m_tail].emplace(std::forward<Arguments>(arguments)...);
            }
            catch (const std::bad_alloc&)
            {
                return Unexpected{Error{ErrorCode::OutOfMemory, "RingBuffer element allocation failed"}};
            }
            catch (...)
            {
                return Unexpected{Error{ErrorCode::Internal, "RingBuffer element construction failed"}};
            }
            m_tail = (m_tail + 1) % CapacityValue;
            ++m_size;
            return {};
        }

        [[nodiscard]] Result<void> Push(const T& value)
            requires std::copy_constructible<T>
        {
            return Emplace(value);
        }

        [[nodiscard]] Result<void> Push(T&& value)
        {
            return Emplace(std::move(value));
        }

        [[nodiscard]] Result<T> Pop()
        {
            if (IsEmpty())
            {
                return Unexpected{Error{ErrorCode::NotFound, "RingBuffer is empty"}};
            }
            T result = std::move(*m_values[m_head]);
            m_values[m_head].reset();
            m_head = (m_head + 1) % CapacityValue;
            --m_size;
            return result;
        }

        void Clear() noexcept
        {
            for (std::optional<T>& value : m_values)
            {
                value.reset();
            }
            m_head = 0;
            m_tail = 0;
            m_size = 0;
        }

    private:
        std::array<std::optional<T>, CapacityValue> m_values;
        std::size_t m_head = 0;
        std::size_t m_tail = 0;
        std::size_t m_size = 0;
    };
}

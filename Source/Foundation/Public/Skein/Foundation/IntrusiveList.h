#pragma once

#include <Skein/Foundation/Result.h>

#include <cstddef>
#include <iterator>

namespace Skein
{
    class IntrusiveListNode;

    template<typename T, IntrusiveListNode T::* NodeMember>
    class IntrusiveList;

    class IntrusiveListNode final
    {
        template<typename T, IntrusiveListNode T::* NodeMember>
        friend class IntrusiveList;

    public:
        IntrusiveListNode() noexcept = default;
        IntrusiveListNode(const IntrusiveListNode&) = delete;
        IntrusiveListNode& operator=(const IntrusiveListNode&) = delete;
        IntrusiveListNode(IntrusiveListNode&&) = delete;
        IntrusiveListNode& operator=(IntrusiveListNode&&) = delete;

        ~IntrusiveListNode()
        {
            (void)SKEIN_ASSERT_MESSAGE(m_list == nullptr, "intrusive node destroyed while linked");
        }

        [[nodiscard]] bool IsLinked() const noexcept { return m_list != nullptr; }

    private:
        IntrusiveListNode* m_previous = nullptr;
        IntrusiveListNode* m_next = nullptr;
        void* m_owner = nullptr;
        const void* m_list = nullptr;
    };

    template<typename T, IntrusiveListNode T::* NodeMember>
    class IntrusiveList final
    {
    public:
        class Iterator final
        {
        public:
            using difference_type = std::ptrdiff_t;
            using value_type = T;
            using pointer = T*;
            using reference = T&;
            using iterator_category = std::forward_iterator_tag;

            Iterator() noexcept = default;

            [[nodiscard]] reference operator*() const noexcept
            {
                return *static_cast<T*>(m_node->m_owner);
            }

            [[nodiscard]] pointer operator->() const noexcept
            {
                return static_cast<T*>(m_node->m_owner);
            }

            Iterator& operator++() noexcept
            {
                m_node = m_node->m_next;
                return *this;
            }

            Iterator operator++(int) noexcept
            {
                Iterator previous = *this;
                ++(*this);
                return previous;
            }

            [[nodiscard]] friend bool operator==(
                const Iterator&,
                const Iterator&) noexcept = default;

        private:
            friend class IntrusiveList;
            explicit Iterator(IntrusiveListNode* node) noexcept : m_node(node) {}
            IntrusiveListNode* m_node = nullptr;
        };

        IntrusiveList() noexcept = default;
        IntrusiveList(const IntrusiveList&) = delete;
        IntrusiveList& operator=(const IntrusiveList&) = delete;
        IntrusiveList(IntrusiveList&&) = delete;
        IntrusiveList& operator=(IntrusiveList&&) = delete;

        ~IntrusiveList() { Clear(); }

        [[nodiscard]] bool IsEmpty() const noexcept { return m_size == 0; }
        [[nodiscard]] std::size_t Size() const noexcept { return m_size; }
        [[nodiscard]] Iterator begin() noexcept { return Iterator{m_head}; }
        [[nodiscard]] Iterator end() noexcept { return Iterator{}; }

        [[nodiscard]] Result<void> PushBack(T& value) noexcept
        {
            IntrusiveListNode& node = value.*NodeMember;
            if (node.IsLinked())
            {
                return Unexpected{Error{ErrorCode::AlreadyExists, "intrusive node is already linked"}};
            }
            node.m_owner = &value;
            node.m_list = this;
            node.m_previous = m_tail;
            if (m_tail != nullptr)
            {
                m_tail->m_next = &node;
            }
            else
            {
                m_head = &node;
            }
            m_tail = &node;
            ++m_size;
            return {};
        }

        [[nodiscard]] Result<void> Remove(T& value) noexcept
        {
            IntrusiveListNode& node = value.*NodeMember;
            if (node.m_list != this)
            {
                return Unexpected{Error{ErrorCode::NotFound, "intrusive node is not in this list"}};
            }
            Unlink(node);
            return {};
        }

        void Clear() noexcept
        {
            IntrusiveListNode* node = m_head;
            while (node != nullptr)
            {
                IntrusiveListNode* const next = node->m_next;
                ResetNode(*node);
                node = next;
            }
            m_head = nullptr;
            m_tail = nullptr;
            m_size = 0;
        }

    private:
        void Unlink(IntrusiveListNode& node) noexcept
        {
            if (node.m_previous != nullptr)
            {
                node.m_previous->m_next = node.m_next;
            }
            else
            {
                m_head = node.m_next;
            }
            if (node.m_next != nullptr)
            {
                node.m_next->m_previous = node.m_previous;
            }
            else
            {
                m_tail = node.m_previous;
            }
            ResetNode(node);
            --m_size;
        }

        static void ResetNode(IntrusiveListNode& node) noexcept
        {
            node.m_previous = nullptr;
            node.m_next = nullptr;
            node.m_owner = nullptr;
            node.m_list = nullptr;
        }

        IntrusiveListNode* m_head = nullptr;
        IntrusiveListNode* m_tail = nullptr;
        std::size_t m_size = 0;
    };
}

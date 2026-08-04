#pragma once

#include <Skein/Foundation/Assert.h>
#include <Skein/Foundation/Result.h>

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>

namespace Skein
{
    enum class MemoryTag : u8
    {
        Persistent,
        Transient,
        Frame,
        Scratch,
        Count
    };

    inline constexpr usize MemoryTagCount = static_cast<usize>(MemoryTag::Count);

    struct AllocationRequest final
    {
        usize Size = 0;
        usize Alignment = alignof(std::max_align_t);
        MemoryTag Tag = MemoryTag::Persistent;
    };

    struct Allocation final
    {
        void* Data = nullptr;
        usize Size = 0;
        usize Alignment = 0;
        MemoryTag Tag = MemoryTag::Persistent;
        u64 Generation = 0;

        [[nodiscard]] explicit operator bool() const noexcept { return Data != nullptr; }
    };

    class IAllocator
    {
    public:
        virtual ~IAllocator() = default;
        [[nodiscard]] virtual Result<Allocation> Allocate(AllocationRequest request) = 0;
        [[nodiscard]] virtual Result<void> Deallocate(Allocation& allocation) noexcept = 0;
    };

    [[nodiscard]] IAllocator& GetSystemAllocator() noexcept;

    struct MemoryBudget final
    {
        usize Limit = std::numeric_limits<usize>::max();
    };

    struct MemoryTagDiagnostics final
    {
        usize CurrentBytes = 0;
        usize PeakBytes = 0;
        usize TotalBytes = 0;
        usize ActiveAllocations = 0;
        usize AllocationCount = 0;
        usize FailureCount = 0;
    };

    struct MemoryDiagnostics final
    {
        std::array<MemoryTagDiagnostics, MemoryTagCount> Tags{};
        usize StartupEvents = 0;
        usize OperationEvents = 0;
        usize FailureEvents = 0;
        usize ShutdownEvents = 0;
        usize LeakedAllocations = 0;
        usize LeakedBytes = 0;
        bool IsInitialised = false;
    };

    struct MemoryArchitectureConfig final
    {
        static constexpr usize MaximumTrackedAllocations = 4096;

        std::array<MemoryBudget, MemoryTagCount> Budgets{};
        usize TrackedAllocationCapacity = MaximumTrackedAllocations;
    };

    class MemoryArchitectureService final : public IAllocator
    {
    public:
        MemoryArchitectureService() noexcept = default;
        MemoryArchitectureService(const MemoryArchitectureService&) = delete;
        MemoryArchitectureService& operator=(const MemoryArchitectureService&) = delete;
        ~MemoryArchitectureService() override;

        [[nodiscard]] Result<void> Initialise(const MemoryArchitectureConfig& config);
        [[nodiscard]] Result<Allocation> Allocate(AllocationRequest request) override;
        [[nodiscard]] Result<void> Deallocate(Allocation& allocation) noexcept override;
        void Shutdown() noexcept;

        [[nodiscard]] MemoryDiagnostics GetDiagnostics() const noexcept;

    private:
        struct Record final
        {
            Allocation Block{};
            bool IsActive = false;
        };

        [[nodiscard]] static bool IsValidTag(MemoryTag tag) noexcept;
        [[nodiscard]] static bool IsValidAlignment(usize alignment) noexcept;
        void RecordFailure(MemoryTag tag) noexcept;

        mutable std::mutex m_mutex;
        MemoryArchitectureConfig m_config{};
        MemoryDiagnostics m_diagnostics{};
        std::array<Record, MemoryArchitectureConfig::MaximumTrackedAllocations> m_records{};
        u64 m_nextGeneration = 1;
    };

    namespace Detail
    {
        struct StlAllocationHeader final
        {
            Allocation Block{};
        };
    }

    template<typename T>
    class SkeinAllocator final
    {
    public:
        using value_type = T;
        using propagate_on_container_move_assignment = std::true_type;
        using is_always_equal = std::false_type;

        SkeinAllocator() noexcept = default;

        explicit SkeinAllocator(
            IAllocator& allocator,
            const MemoryTag tag = MemoryTag::Persistent) noexcept
            : m_allocator(&allocator),
              m_tag(tag)
        {
        }

        template<typename U>
        SkeinAllocator(const SkeinAllocator<U>& other) noexcept
            : m_allocator(other.Allocator()),
              m_tag(other.Tag())
        {
        }

        [[nodiscard]] T* allocate(const usize count)
        {
            if (count > max_size())
            {
                throw std::bad_array_new_length{};
            }

            constexpr usize valueAlignment = alignof(T);
            constexpr usize headerAlignment = alignof(Detail::StlAllocationHeader);
            constexpr usize allocationAlignment =
                valueAlignment > headerAlignment ? valueAlignment : headerAlignment;
            constexpr usize padding = allocationAlignment - 1;

            const usize valueBytes = count * sizeof(T);
            if (valueBytes > std::numeric_limits<usize>::max() -
                    sizeof(Detail::StlAllocationHeader) - padding)
            {
                throw std::bad_array_new_length{};
            }

            const usize totalBytes =
                valueBytes + sizeof(Detail::StlAllocationHeader) + padding;
            Result<Allocation> result = m_allocator->Allocate({
                totalBytes,
                allocationAlignment,
                m_tag});
            if (!result)
            {
                throw std::bad_alloc{};
            }

            Allocation block = result.Value();
            auto* const base = static_cast<Byte*>(block.Data);
            void* candidate = base + sizeof(Detail::StlAllocationHeader);
            usize space = totalBytes - sizeof(Detail::StlAllocationHeader);
            void* const aligned = std::align(valueAlignment, valueBytes, candidate, space);
            if (aligned == nullptr)
            {
                (void)m_allocator->Deallocate(block);
                throw std::bad_alloc{};
            }

            auto* const header = reinterpret_cast<Detail::StlAllocationHeader*>(
                static_cast<Byte*>(aligned) - sizeof(Detail::StlAllocationHeader));
            std::construct_at(header, Detail::StlAllocationHeader{block});
            return static_cast<T*>(aligned);
        }

        void deallocate(T* const pointer, const usize) noexcept
        {
            if (pointer == nullptr)
            {
                return;
            }
            auto* const header = reinterpret_cast<Detail::StlAllocationHeader*>(
                reinterpret_cast<Byte*>(pointer) - sizeof(Detail::StlAllocationHeader));
            Allocation block = header->Block;
            std::destroy_at(header);
            const Result<void> result = m_allocator->Deallocate(block);
            (void)SKEIN_VERIFY_MESSAGE(result.HasValue(), "SkeinAllocator deallocation failed");
        }

        [[nodiscard]] constexpr usize max_size() const noexcept
        {
            return std::numeric_limits<usize>::max() / sizeof(T);
        }

        [[nodiscard]] IAllocator* Allocator() const noexcept { return m_allocator; }
        [[nodiscard]] MemoryTag Tag() const noexcept { return m_tag; }

        template<typename U>
        [[nodiscard]] bool operator==(const SkeinAllocator<U>& other) const noexcept
        {
            return m_allocator == other.Allocator() && m_tag == other.Tag();
        }

    private:
        template<typename>
        friend class SkeinAllocator;

        IAllocator* m_allocator = &GetSystemAllocator();
        MemoryTag m_tag = MemoryTag::Persistent;
    };
}

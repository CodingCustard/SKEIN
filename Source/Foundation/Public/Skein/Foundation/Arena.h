#pragma once

#include <Skein/Foundation/Memory.h>

#include <array>
#include <thread>

namespace Skein
{
    struct ArenaMarker final
    {
        usize Offset = 0;
        u64 Generation = 0;
    };

    struct ArenaDiagnostics final
    {
        usize Capacity = 0;
        usize UsedBytes = 0;
        usize PeakBytes = 0;
        usize AlignmentWasteBytes = 0;
        usize ActiveAllocations = 0;
        usize AllocationCount = 0;
        usize FailureCount = 0;
        u64 Generation = 0;
    };

    class LinearArena final : public IAllocator
    {
    public:
        static constexpr usize MaximumActiveAllocations = 1024;

        LinearArena() noexcept = default;
        LinearArena(const LinearArena&) = delete;
        LinearArena& operator=(const LinearArena&) = delete;
        ~LinearArena() override;

        [[nodiscard]] Result<void> Initialise(
            IAllocator& backingAllocator,
            usize capacity,
            MemoryTag tag,
            usize maximumAlignment = 64);
        void Shutdown() noexcept;

        [[nodiscard]] Result<Allocation> Allocate(AllocationRequest request) override;
        [[nodiscard]] Result<void> Deallocate(Allocation& allocation) noexcept override;
        [[nodiscard]] Result<void> Reset() noexcept;
        [[nodiscard]] ArenaMarker Mark() const noexcept;
        [[nodiscard]] Result<void> Restore(ArenaMarker marker) noexcept;
        [[nodiscard]] ArenaDiagnostics GetDiagnostics() const noexcept;

    private:
        struct Record final
        {
            Allocation Block{};
            usize EndOffset = 0;
            bool IsActive = false;
        };

        [[nodiscard]] bool IsOwnerThread() const noexcept;
        [[nodiscard]] Result<void> ValidateOwner() const noexcept;
        void RecordFailure() noexcept;

        IAllocator* m_backingAllocator = nullptr;
        Allocation m_storage{};
        MemoryTag m_tag = MemoryTag::Transient;
        usize m_maximumAlignment = 0;
        std::thread::id m_ownerThread{};
        ArenaDiagnostics m_diagnostics{};
        std::array<Record, MaximumActiveAllocations> m_records{};
        u64 m_nextAllocationGeneration = 1;
        bool m_isInitialised = false;
    };

    class FrameAllocator final : public IAllocator
    {
    public:
        [[nodiscard]] Result<void> Initialise(
            IAllocator& backingAllocator,
            usize capacity,
            usize maximumAlignment = 64);
        void Shutdown() noexcept;
        [[nodiscard]] Result<void> BeginFrame() noexcept;
        [[nodiscard]] u64 FrameIndex() const noexcept { return m_frameIndex; }

        [[nodiscard]] Result<Allocation> Allocate(AllocationRequest request) override;
        [[nodiscard]] Result<void> Deallocate(Allocation& allocation) noexcept override;
        [[nodiscard]] ArenaDiagnostics GetDiagnostics() const noexcept;

    private:
        LinearArena m_arena;
        u64 m_frameIndex = 0;
    };

    class ThreadScratchAllocator final : public IAllocator
    {
    public:
        [[nodiscard]] Result<void> Initialise(
            IAllocator& backingAllocator,
            usize capacity,
            usize maximumAlignment = 64);
        void Shutdown() noexcept;

        [[nodiscard]] Result<Allocation> Allocate(AllocationRequest request) override;
        [[nodiscard]] Result<void> Deallocate(Allocation& allocation) noexcept override;
        [[nodiscard]] ArenaMarker Mark() const noexcept;
        [[nodiscard]] Result<void> Restore(ArenaMarker marker) noexcept;
        [[nodiscard]] ArenaDiagnostics GetDiagnostics() const noexcept;

    private:
        LinearArena m_arena;
    };

    class ScratchScope final
    {
    public:
        explicit ScratchScope(ThreadScratchAllocator& allocator) noexcept;
        ScratchScope(const ScratchScope&) = delete;
        ScratchScope& operator=(const ScratchScope&) = delete;
        ~ScratchScope();

    private:
        ThreadScratchAllocator& m_allocator;
        ArenaMarker m_marker{};
    };

    [[nodiscard]] ThreadScratchAllocator& GetThreadScratchAllocator() noexcept;
}

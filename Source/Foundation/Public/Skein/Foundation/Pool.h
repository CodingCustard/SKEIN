#pragma once

#include <Skein/Foundation/Memory.h>

#include <array>
#include <thread>

namespace Skein
{
    struct PoolDiagnostics final
    {
        usize BlockSize = 0;
        usize BlockCount = 0;
        usize ActiveBlocks = 0;
        usize PeakActiveBlocks = 0;
        usize AllocationCount = 0;
        usize FailureCount = 0;
    };

    class FixedBlockPool final : public IAllocator
    {
    public:
        static constexpr usize MaximumBlockCount = 1024;

        FixedBlockPool() noexcept = default;
        FixedBlockPool(const FixedBlockPool&) = delete;
        FixedBlockPool& operator=(const FixedBlockPool&) = delete;
        ~FixedBlockPool() override;

        [[nodiscard]] Result<void> Initialise(
            IAllocator& backingAllocator,
            usize blockSize,
            usize blockCount,
            usize blockAlignment,
            MemoryTag tag = MemoryTag::Persistent);
        void Shutdown() noexcept;

        [[nodiscard]] Result<Allocation> Allocate(AllocationRequest request) override;
        [[nodiscard]] Result<void> Deallocate(Allocation& allocation) noexcept override;
        [[nodiscard]] PoolDiagnostics GetDiagnostics() const noexcept;

    private:
        [[nodiscard]] bool IsOwnerThread() const noexcept;
        void RecordFailure() noexcept;

        IAllocator* m_backingAllocator = nullptr;
        Allocation m_storage{};
        MemoryTag m_tag = MemoryTag::Persistent;
        usize m_blockStride = 0;
        usize m_blockAlignment = 0;
        std::thread::id m_ownerThread{};
        PoolDiagnostics m_diagnostics{};
        std::array<usize, MaximumBlockCount> m_freeIndices{};
        std::array<Allocation, MaximumBlockCount> m_activeBlocks{};
        usize m_freeCount = 0;
        u64 m_nextGeneration = 1;
        bool m_isInitialised = false;
    };
}

#include <Skein/Foundation/Pool.h>

#include <algorithm>

namespace Skein
{
    namespace
    {
        [[nodiscard]] bool IsPowerOfTwo(const usize value) noexcept
        {
            return value != 0 && (value & (value - 1)) == 0;
        }

        [[nodiscard]] usize AlignUp(const usize value, const usize alignment) noexcept
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }
    }

    FixedBlockPool::~FixedBlockPool()
    {
        Shutdown();
    }

    Result<void> FixedBlockPool::Initialise(
        IAllocator& backingAllocator,
        const usize blockSize,
        const usize blockCount,
        const usize blockAlignment,
        const MemoryTag tag)
    {
        if (m_isInitialised)
        {
            return Unexpected{Error{ErrorCode::InvalidState, "pool is already initialised"}};
        }
        if (blockSize == 0 || blockCount == 0 || blockCount > MaximumBlockCount ||
            !IsPowerOfTwo(blockAlignment) || static_cast<usize>(tag) >= MemoryTagCount ||
            blockSize > std::numeric_limits<usize>::max() - (blockAlignment - 1))
        {
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid pool configuration"}};
        }

        const usize stride = AlignUp(blockSize, blockAlignment);
        if (stride > std::numeric_limits<usize>::max() / blockCount)
        {
            return Unexpected{Error{ErrorCode::InvalidArgument, "pool capacity overflow"}};
        }

        Result<Allocation> storage = backingAllocator.Allocate({
            stride * blockCount,
            blockAlignment,
            tag});
        if (!storage)
        {
            return Unexpected{storage.ErrorValue()};
        }

        m_backingAllocator = &backingAllocator;
        m_storage = storage.Value();
        m_tag = tag;
        m_blockStride = stride;
        m_blockAlignment = blockAlignment;
        m_ownerThread = std::this_thread::get_id();
        m_diagnostics = {};
        m_diagnostics.BlockSize = blockSize;
        m_diagnostics.BlockCount = blockCount;
        m_freeCount = blockCount;
        m_nextGeneration = 1;
        for (usize index = 0; index < blockCount; ++index)
        {
            m_freeIndices[index] = blockCount - index - 1;
            m_activeBlocks[index] = {};
        }
        m_isInitialised = true;
        return {};
    }

    void FixedBlockPool::Shutdown() noexcept
    {
        if (!m_isInitialised)
        {
            return;
        }

        for (usize index = 0; index < m_diagnostics.BlockCount; ++index)
        {
            m_activeBlocks[index] = {};
        }
        (void)m_backingAllocator->Deallocate(m_storage);
        m_backingAllocator = nullptr;
        m_freeCount = 0;
        m_diagnostics.ActiveBlocks = 0;
        m_isInitialised = false;
    }

    Result<Allocation> FixedBlockPool::Allocate(const AllocationRequest request)
    {
        if (!m_isInitialised)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "pool is not initialised"}};
        }
        if (!IsOwnerThread())
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "pool used from non-owner thread"}};
        }
        if (request.Size == 0 || request.Size > m_diagnostics.BlockSize ||
            !IsPowerOfTwo(request.Alignment) || request.Alignment > m_blockAlignment ||
            request.Tag != m_tag)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid pool allocation request"}};
        }
        if (m_freeCount == 0)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::OutOfMemory, "pool is exhausted"}};
        }
        if (m_nextGeneration == std::numeric_limits<u64>::max())
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::OutOfMemory, "pool generation exhausted"}};
        }

        const usize index = m_freeIndices[--m_freeCount];
        auto* const data = static_cast<Byte*>(m_storage.Data) + index * m_blockStride;
        Allocation block{
            data,
            request.Size,
            request.Alignment,
            request.Tag,
            m_nextGeneration++};
        m_activeBlocks[index] = block;
        ++m_diagnostics.ActiveBlocks;
        m_diagnostics.PeakActiveBlocks = std::max(
            m_diagnostics.PeakActiveBlocks,
            m_diagnostics.ActiveBlocks);
        ++m_diagnostics.AllocationCount;
        return block;
    }

    Result<void> FixedBlockPool::Deallocate(Allocation& allocation) noexcept
    {
        if (!m_isInitialised)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "pool is not initialised"}};
        }
        if (!IsOwnerThread())
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "pool used from non-owner thread"}};
        }

        auto* const base = static_cast<Byte*>(m_storage.Data);
        const auto baseAddress = reinterpret_cast<std::uintptr_t>(base);
        const auto pointerAddress = reinterpret_cast<std::uintptr_t>(allocation.Data);
        const usize storageSize = m_blockStride * m_diagnostics.BlockCount;
        if (pointerAddress < baseAddress || pointerAddress - baseAddress >= storageSize)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "allocation is outside pool"}};
        }
        const usize offset = pointerAddress - baseAddress;
        if (offset % m_blockStride != 0)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "allocation is not a pool block"}};
        }

        const usize index = offset / m_blockStride;
        const Allocation& active = m_activeBlocks[index];
        if (!active || active.Generation != allocation.Generation ||
            active.Size != allocation.Size || active.Alignment != allocation.Alignment ||
            active.Tag != allocation.Tag)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "stale or mismatched pool allocation"}};
        }

        m_activeBlocks[index] = {};
        m_freeIndices[m_freeCount++] = index;
        --m_diagnostics.ActiveBlocks;
        allocation = {};
        return {};
    }

    PoolDiagnostics FixedBlockPool::GetDiagnostics() const noexcept
    {
        return m_diagnostics;
    }

    bool FixedBlockPool::IsOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == m_ownerThread;
    }

    void FixedBlockPool::RecordFailure() noexcept
    {
        ++m_diagnostics.FailureCount;
    }
}

#include <Skein/Foundation/Arena.h>

#include <algorithm>
#include <cstdint>

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

    LinearArena::~LinearArena()
    {
        Shutdown();
    }

    Result<void> LinearArena::Initialise(
        IAllocator& backingAllocator,
        const usize capacity,
        const MemoryTag tag,
        const usize maximumAlignment)
    {
        if (m_isInitialised)
        {
            return Unexpected{Error{ErrorCode::InvalidState, "arena is already initialised"}};
        }
        if (capacity == 0 || !IsPowerOfTwo(maximumAlignment) ||
            static_cast<usize>(tag) >= MemoryTagCount)
        {
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid arena configuration"}};
        }

        Result<Allocation> storage = backingAllocator.Allocate({
            capacity,
            maximumAlignment,
            tag});
        if (!storage)
        {
            return Unexpected{storage.ErrorValue()};
        }

        m_backingAllocator = &backingAllocator;
        m_storage = storage.Value();
        m_tag = tag;
        m_maximumAlignment = maximumAlignment;
        m_ownerThread = std::this_thread::get_id();
        m_diagnostics = {};
        m_diagnostics.Capacity = capacity;
        m_diagnostics.Generation = 1;
        m_nextAllocationGeneration = 1;
        m_isInitialised = true;
        return {};
    }

    void LinearArena::Shutdown() noexcept
    {
        if (!m_isInitialised)
        {
            return;
        }

        for (Record& record : m_records)
        {
            record = {};
        }
        (void)m_backingAllocator->Deallocate(m_storage);
        m_backingAllocator = nullptr;
        m_diagnostics.UsedBytes = 0;
        m_diagnostics.ActiveAllocations = 0;
        m_isInitialised = false;
    }

    Result<Allocation> LinearArena::Allocate(const AllocationRequest request)
    {
        Result<void> ownerResult = ValidateOwner();
        if (!ownerResult)
        {
            RecordFailure();
            return Unexpected{ownerResult.ErrorValue()};
        }
        if (request.Size == 0 || !IsPowerOfTwo(request.Alignment) ||
            request.Alignment > m_maximumAlignment || request.Tag != m_tag)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid arena allocation request"}};
        }

        Record* freeRecord = nullptr;
        for (Record& record : m_records)
        {
            if (!record.IsActive)
            {
                freeRecord = &record;
                break;
            }
        }
        if (freeRecord == nullptr)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::OutOfMemory, "arena tracking capacity exceeded"}};
        }
        if (m_nextAllocationGeneration == std::numeric_limits<u64>::max())
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::OutOfMemory, "arena generation exhausted"}};
        }

        const auto baseAddress = reinterpret_cast<std::uintptr_t>(m_storage.Data);
        if (m_diagnostics.UsedBytes > std::numeric_limits<usize>::max() - baseAddress)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::OutOfMemory, "arena address overflow"}};
        }
        const usize currentAddress = baseAddress + m_diagnostics.UsedBytes;
        if (currentAddress > std::numeric_limits<usize>::max() - (request.Alignment - 1))
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::OutOfMemory, "arena alignment overflow"}};
        }
        const usize alignedAddress = AlignUp(currentAddress, request.Alignment);
        const usize alignedOffset = alignedAddress - baseAddress;
        if (alignedOffset > m_diagnostics.Capacity ||
            request.Size > m_diagnostics.Capacity - alignedOffset)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::OutOfMemory, "arena capacity exceeded"}};
        }

        Allocation block{
            reinterpret_cast<void*>(alignedAddress),
            request.Size,
            request.Alignment,
            request.Tag,
            m_nextAllocationGeneration++};
        freeRecord->Block = block;
        freeRecord->EndOffset = alignedOffset + request.Size;
        freeRecord->IsActive = true;

        const usize alignmentWaste = alignedOffset - m_diagnostics.UsedBytes;
        if (m_diagnostics.AlignmentWasteBytes >
            std::numeric_limits<usize>::max() - alignmentWaste)
        {
            m_diagnostics.AlignmentWasteBytes = std::numeric_limits<usize>::max();
        }
        else
        {
            m_diagnostics.AlignmentWasteBytes += alignmentWaste;
        }
        m_diagnostics.UsedBytes = freeRecord->EndOffset;
        m_diagnostics.PeakBytes = std::max(
            m_diagnostics.PeakBytes,
            m_diagnostics.UsedBytes);
        ++m_diagnostics.ActiveAllocations;
        ++m_diagnostics.AllocationCount;
        return block;
    }

    Result<void> LinearArena::Deallocate(Allocation& allocation) noexcept
    {
        Result<void> ownerResult = ValidateOwner();
        if (!ownerResult)
        {
            RecordFailure();
            return ownerResult;
        }

        for (Record& record : m_records)
        {
            if (record.IsActive && record.Block.Data == allocation.Data &&
                record.Block.Generation == allocation.Generation &&
                record.Block.Size == allocation.Size &&
                record.Block.Alignment == allocation.Alignment &&
                record.Block.Tag == allocation.Tag)
            {
                record.IsActive = false;
                --m_diagnostics.ActiveAllocations;
                allocation = {};
                return {};
            }
        }

        RecordFailure();
        return Unexpected{Error{
            ErrorCode::InvalidArgument,
            "stale or mismatched arena allocation"}};
    }

    Result<void> LinearArena::Reset() noexcept
    {
        Result<void> ownerResult = ValidateOwner();
        if (!ownerResult)
        {
            RecordFailure();
            return ownerResult;
        }

        for (Record& record : m_records)
        {
            record = {};
        }
        m_diagnostics.UsedBytes = 0;
        m_diagnostics.AlignmentWasteBytes = 0;
        m_diagnostics.ActiveAllocations = 0;
        ++m_diagnostics.Generation;
        return {};
    }

    ArenaMarker LinearArena::Mark() const noexcept
    {
        if (!m_isInitialised || !IsOwnerThread())
        {
            return {};
        }
        return ArenaMarker{m_diagnostics.UsedBytes, m_diagnostics.Generation};
    }

    Result<void> LinearArena::Restore(const ArenaMarker marker) noexcept
    {
        Result<void> ownerResult = ValidateOwner();
        if (!ownerResult)
        {
            RecordFailure();
            return ownerResult;
        }
        if (marker.Generation != m_diagnostics.Generation ||
            marker.Offset > m_diagnostics.UsedBytes)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid arena marker"}};
        }

        for (Record& record : m_records)
        {
            if (record.IsActive && record.EndOffset > marker.Offset)
            {
                record.IsActive = false;
                --m_diagnostics.ActiveAllocations;
            }
        }
        m_diagnostics.UsedBytes = marker.Offset;
        return {};
    }

    ArenaDiagnostics LinearArena::GetDiagnostics() const noexcept
    {
        return m_diagnostics;
    }

    bool LinearArena::IsOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == m_ownerThread;
    }

    Result<void> LinearArena::ValidateOwner() const noexcept
    {
        if (!m_isInitialised)
        {
            return Unexpected{Error{ErrorCode::InvalidState, "arena is not initialised"}};
        }
        if (!IsOwnerThread())
        {
            return Unexpected{Error{ErrorCode::InvalidState, "arena used from non-owner thread"}};
        }
        return {};
    }

    void LinearArena::RecordFailure() noexcept
    {
        ++m_diagnostics.FailureCount;
    }

    Result<void> FrameAllocator::Initialise(
        IAllocator& backingAllocator,
        const usize capacity,
        const usize maximumAlignment)
    {
        m_frameIndex = 0;
        return m_arena.Initialise(
            backingAllocator,
            capacity,
            MemoryTag::Frame,
            maximumAlignment);
    }

    void FrameAllocator::Shutdown() noexcept
    {
        m_arena.Shutdown();
    }

    Result<void> FrameAllocator::BeginFrame() noexcept
    {
        Result<void> result = m_arena.Reset();
        if (result)
        {
            ++m_frameIndex;
        }
        return result;
    }

    Result<Allocation> FrameAllocator::Allocate(AllocationRequest request)
    {
        request.Tag = MemoryTag::Frame;
        return m_arena.Allocate(request);
    }

    Result<void> FrameAllocator::Deallocate(Allocation& allocation) noexcept
    {
        return m_arena.Deallocate(allocation);
    }

    ArenaDiagnostics FrameAllocator::GetDiagnostics() const noexcept
    {
        return m_arena.GetDiagnostics();
    }

    Result<void> ThreadScratchAllocator::Initialise(
        IAllocator& backingAllocator,
        const usize capacity,
        const usize maximumAlignment)
    {
        return m_arena.Initialise(
            backingAllocator,
            capacity,
            MemoryTag::Scratch,
            maximumAlignment);
    }

    void ThreadScratchAllocator::Shutdown() noexcept
    {
        m_arena.Shutdown();
    }

    Result<Allocation> ThreadScratchAllocator::Allocate(AllocationRequest request)
    {
        request.Tag = MemoryTag::Scratch;
        return m_arena.Allocate(request);
    }

    Result<void> ThreadScratchAllocator::Deallocate(Allocation& allocation) noexcept
    {
        return m_arena.Deallocate(allocation);
    }

    ArenaMarker ThreadScratchAllocator::Mark() const noexcept
    {
        return m_arena.Mark();
    }

    Result<void> ThreadScratchAllocator::Restore(const ArenaMarker marker) noexcept
    {
        return m_arena.Restore(marker);
    }

    ArenaDiagnostics ThreadScratchAllocator::GetDiagnostics() const noexcept
    {
        return m_arena.GetDiagnostics();
    }

    ScratchScope::ScratchScope(ThreadScratchAllocator& allocator) noexcept
        : m_allocator(allocator),
          m_marker(allocator.Mark())
    {
    }

    ScratchScope::~ScratchScope()
    {
        const Result<void> result = m_allocator.Restore(m_marker);
        (void)SKEIN_VERIFY_MESSAGE(result.HasValue(), "scratch scope restore failed");
    }

    ThreadScratchAllocator& GetThreadScratchAllocator() noexcept
    {
        thread_local ThreadScratchAllocator allocator;
        return allocator;
    }
}

#include <Skein/Foundation/Memory.h>

#include <Skein/Foundation/Build.h>
#include <Skein/Foundation/Log.h>

#include <algorithm>

namespace Skein
{
    namespace
    {
        void TraceMemory(const StringView message)
        {
#if SKEIN_ENABLE_TRACING
            Log(LogLevel::Trace, "Memory", message);
#else
            (void)message;
#endif
        }

        class SystemAllocator final : public IAllocator
        {
        public:
            [[nodiscard]] Result<Allocation> Allocate(const AllocationRequest request) override
            {
                if (request.Size == 0 || request.Alignment == 0 ||
                    (request.Alignment & (request.Alignment - 1)) != 0 ||
                    static_cast<usize>(request.Tag) >= MemoryTagCount)
                {
                    return Unexpected{Error{
                        ErrorCode::InvalidArgument,
                        "invalid system allocation request"}};
                }

                void* data = nullptr;
                if (request.Alignment <= alignof(std::max_align_t))
                {
                    data = ::operator new(request.Size, std::nothrow);
                }
                else
                {
                    data = ::operator new(
                        request.Size,
                        std::align_val_t{request.Alignment},
                        std::nothrow);
                }
                if (data == nullptr)
                {
                    return Unexpected{Error{
                        ErrorCode::OutOfMemory,
                        "system allocation failed"}};
                }

                return Allocation{
                    data,
                    request.Size,
                    request.Alignment,
                    request.Tag,
                    m_nextGeneration.fetch_add(1, std::memory_order_relaxed)};
            }

            [[nodiscard]] Result<void> Deallocate(Allocation& allocation) noexcept override
            {
                if (!allocation)
                {
                    return Unexpected{Error{
                        ErrorCode::InvalidArgument,
                        "invalid system deallocation"}};
                }

                if (allocation.Alignment <= alignof(std::max_align_t))
                {
                    ::operator delete(allocation.Data);
                }
                else
                {
                    ::operator delete(
                        allocation.Data,
                        std::align_val_t{allocation.Alignment});
                }
                allocation = {};
                return {};
            }

        private:
            std::atomic<u64> m_nextGeneration = 1;
        };

        [[nodiscard]] usize TagIndex(const MemoryTag tag) noexcept
        {
            return static_cast<usize>(tag);
        }
    }

    IAllocator& GetSystemAllocator() noexcept
    {
        static SystemAllocator allocator;
        return allocator;
    }

    MemoryArchitectureService::~MemoryArchitectureService()
    {
        Shutdown();
    }

    Result<void> MemoryArchitectureService::Initialise(
        const MemoryArchitectureConfig& config)
    {
        std::scoped_lock lock{m_mutex};
        if (m_diagnostics.IsInitialised)
        {
            return Unexpected{Error{
                ErrorCode::InvalidState,
                "memory architecture is already initialised"}};
        }
        if (config.TrackedAllocationCapacity == 0 ||
            config.TrackedAllocationCapacity > m_records.size())
        {
            return Unexpected{Error{
                ErrorCode::InvalidArgument,
                "invalid tracked allocation capacity"}};
        }

        m_config = config;
        m_diagnostics = {};
        m_diagnostics.IsInitialised = true;
        ++m_diagnostics.StartupEvents;
        TraceMemory("memory architecture initialised");
        return {};
    }

    Result<Allocation> MemoryArchitectureService::Allocate(
        const AllocationRequest request)
    {
        std::scoped_lock lock{m_mutex};
        if (!m_diagnostics.IsInitialised)
        {
            ++m_diagnostics.FailureEvents;
            return Unexpected{Error{
                ErrorCode::InvalidState,
                "memory architecture is not initialised"}};
        }
        if (request.Size == 0 || !IsValidAlignment(request.Alignment) ||
            !IsValidTag(request.Tag))
        {
            RecordFailure(request.Tag);
            return Unexpected{Error{
                ErrorCode::InvalidArgument,
                "invalid memory allocation request"}};
        }

        const usize tagIndex = TagIndex(request.Tag);
        MemoryTagDiagnostics& tag = m_diagnostics.Tags[tagIndex];
        const usize limit = m_config.Budgets[tagIndex].Limit;
        if (request.Size > limit || tag.CurrentBytes > limit - request.Size)
        {
            RecordFailure(request.Tag);
            return Unexpected{Error{
                ErrorCode::OutOfMemory,
                "memory tag budget exceeded"}};
        }

        Record* freeRecord = nullptr;
        for (usize index = 0; index < m_config.TrackedAllocationCapacity; ++index)
        {
            if (!m_records[index].IsActive)
            {
                freeRecord = &m_records[index];
                break;
            }
        }
        if (freeRecord == nullptr)
        {
            RecordFailure(request.Tag);
            return Unexpected{Error{
                ErrorCode::OutOfMemory,
                "memory allocation tracking capacity exceeded"}};
        }

        Result<Allocation> backendResult = GetSystemAllocator().Allocate(request);
        if (!backendResult)
        {
            RecordFailure(request.Tag);
            return Unexpected{backendResult.ErrorValue()};
        }

        if (m_nextGeneration == std::numeric_limits<u64>::max())
        {
            Allocation backendBlock = backendResult.Value();
            (void)GetSystemAllocator().Deallocate(backendBlock);
            RecordFailure(request.Tag);
            return Unexpected{Error{
                ErrorCode::OutOfMemory,
                "memory allocation generation exhausted"}};
        }

        Allocation block = backendResult.Value();
        block.Generation = m_nextGeneration++;
        freeRecord->Block = block;
        freeRecord->IsActive = true;

        tag.CurrentBytes += request.Size;
        tag.PeakBytes = std::max(tag.PeakBytes, tag.CurrentBytes);
        if (tag.TotalBytes > std::numeric_limits<usize>::max() - request.Size)
        {
            tag.TotalBytes = std::numeric_limits<usize>::max();
        }
        else
        {
            tag.TotalBytes += request.Size;
        }
        ++tag.ActiveAllocations;
        ++tag.AllocationCount;
        ++m_diagnostics.OperationEvents;
        TraceMemory("allocation succeeded");
        return block;
    }

    Result<void> MemoryArchitectureService::Deallocate(Allocation& allocation) noexcept
    {
        std::scoped_lock lock{m_mutex};
        if (!m_diagnostics.IsInitialised)
        {
            ++m_diagnostics.FailureEvents;
            return Unexpected{Error{
                ErrorCode::InvalidState,
                "memory architecture is not initialised"}};
        }
        if (!allocation || !IsValidTag(allocation.Tag))
        {
            RecordFailure(allocation.Tag);
            return Unexpected{Error{
                ErrorCode::InvalidArgument,
                "invalid memory allocation handle"}};
        }

        Record* record = nullptr;
        for (usize index = 0; index < m_config.TrackedAllocationCapacity; ++index)
        {
            Record& candidate = m_records[index];
            if (candidate.IsActive && candidate.Block.Data == allocation.Data)
            {
                record = &candidate;
                break;
            }
        }
        if (record == nullptr || record->Block.Generation != allocation.Generation ||
            record->Block.Size != allocation.Size ||
            record->Block.Alignment != allocation.Alignment ||
            record->Block.Tag != allocation.Tag)
        {
            RecordFailure(allocation.Tag);
            return Unexpected{Error{
                ErrorCode::InvalidArgument,
                "stale or mismatched memory allocation handle"}};
        }

        const usize tagIndex = TagIndex(record->Block.Tag);
        Result<void> backendResult = GetSystemAllocator().Deallocate(record->Block);
        if (!backendResult)
        {
            RecordFailure(allocation.Tag);
            return Unexpected{backendResult.ErrorValue()};
        }

        MemoryTagDiagnostics& tag = m_diagnostics.Tags[tagIndex];
        tag.CurrentBytes -= allocation.Size;
        --tag.ActiveAllocations;
        record->IsActive = false;
        allocation = {};
        ++m_diagnostics.OperationEvents;
        TraceMemory("deallocation succeeded");
        return {};
    }

    void MemoryArchitectureService::Shutdown() noexcept
    {
        std::scoped_lock lock{m_mutex};
        if (!m_diagnostics.IsInitialised)
        {
            return;
        }

        for (usize index = 0; index < m_config.TrackedAllocationCapacity; ++index)
        {
            Record& record = m_records[index];
            if (!record.IsActive)
            {
                continue;
            }

            ++m_diagnostics.LeakedAllocations;
            m_diagnostics.LeakedBytes += record.Block.Size;
            MemoryTagDiagnostics& tag = m_diagnostics.Tags[TagIndex(record.Block.Tag)];
            tag.CurrentBytes -= record.Block.Size;
            --tag.ActiveAllocations;
            (void)GetSystemAllocator().Deallocate(record.Block);
            record.IsActive = false;
        }

        m_diagnostics.IsInitialised = false;
        ++m_diagnostics.ShutdownEvents;
        TraceMemory("memory architecture shutdown");
    }

    MemoryDiagnostics MemoryArchitectureService::GetDiagnostics() const noexcept
    {
        std::scoped_lock lock{m_mutex};
        return m_diagnostics;
    }

    bool MemoryArchitectureService::IsValidTag(const MemoryTag tag) noexcept
    {
        return static_cast<usize>(tag) < MemoryTagCount;
    }

    bool MemoryArchitectureService::IsValidAlignment(const usize alignment) noexcept
    {
        return alignment != 0 && (alignment & (alignment - 1)) == 0;
    }

    void MemoryArchitectureService::RecordFailure(const MemoryTag tag) noexcept
    {
        if (IsValidTag(tag))
        {
            ++m_diagnostics.Tags[TagIndex(tag)].FailureCount;
        }
        ++m_diagnostics.FailureEvents;
        TraceMemory("memory operation failed");
    }
}

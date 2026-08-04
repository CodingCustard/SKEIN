#include <Skein/Foundation/JobSystem.h>

#include <Skein/Foundation/Build.h>
#include <Skein/Foundation/Log.h>

#include <algorithm>
#include <chrono>
#include <new>
#include <system_error>
#include <thread>

namespace Skein
{
    namespace
    {
        constexpr usize PriorityCount = static_cast<usize>(JobPriority::Count);

        [[nodiscard]] u64 NowNanoseconds() noexcept
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            return static_cast<u64>(std::chrono::duration_cast<Nanoseconds>(now).count());
        }

        [[nodiscard]] bool IsValidTimeout(const Milliseconds timeout) noexcept
        {
            return timeout == Milliseconds::max() || timeout.count() >= 0;
        }

        [[nodiscard]] bool IsTerminal(const JobState state) noexcept
        {
            return state == JobState::Completed || state == JobState::Cancelled;
        }

        void TraceJobSystem(const StringView message)
        {
#if SKEIN_ENABLE_TRACING
            Log(LogLevel::Trace, "JobSystem", message);
#else
            (void)message;
#endif
        }

        struct ParallelForContext final
        {
            ParallelForFunction Function = nullptr;
            void* UserContext = nullptr;
            usize Begin = 0;
            usize End = 0;
        };

        void RunParallelForBatch(void* const context) noexcept
        {
            auto& batch = *static_cast<ParallelForContext*>(context);
            batch.Function(batch.UserContext, batch.Begin, batch.End);
        }
    }

    struct JobSystem::State final
    {
        struct JobRecord final
        {
            JobFunction Function = nullptr;
            void* Context = nullptr;
            std::array<JobHandle, JobSystemConfig::MaximumDependenciesPerJob> Dependencies{};
            usize DependencyCount = 0;
            JobPriority Priority = JobPriority::Normal;
            JobAffinity Affinity = JobAffinity::AnyWorker;
            u32 RequestedWorker = 0;
            JobState CurrentState = JobState::Free;
            u32 Generation = 1;
            u32 ExecutionWorker = JobHandle::InvalidIndex;
            usize WaiterCount = 0;
            u64 SubmittedAt = 0;
            u64 StartedAt = 0;
            u64 FinishedAt = 0;
            bool WasStolen = false;
        };

        struct WorkQueue final
        {
            std::array<u32, JobSystemConfig::MaximumJobs> Values{};
            usize Size = 0;

            [[nodiscard]] bool PushBack(const u32 value) noexcept
            {
                if (Size == Values.size())
                {
                    return false;
                }
                Values[Size++] = value;
                return true;
            }

            [[nodiscard]] bool PopFront(u32& value) noexcept
            {
                if (Size == 0)
                {
                    return false;
                }
                value = Values[0];
                for (usize index = 1; index < Size; ++index)
                {
                    Values[index - 1] = Values[index];
                }
                --Size;
                return true;
            }

            [[nodiscard]] bool RemoveAt(const usize index, u32& value) noexcept
            {
                if (index >= Size)
                {
                    return false;
                }
                value = Values[index];
                for (usize next = index + 1; next < Size; ++next)
                {
                    Values[next - 1] = Values[next];
                }
                --Size;
                return true;
            }
        };

        struct Execution final
        {
            u32 JobIndex = JobHandle::InvalidIndex;
            u32 Generation = 0;
            JobFunction Function = nullptr;
            void* Context = nullptr;
        };

        mutable std::mutex Mutex;
        std::condition_variable WorkCondition;
        std::condition_variable CompletionCondition;
        std::array<JobRecord, JobSystemConfig::MaximumJobs> Jobs{};
        std::array<
            std::array<WorkQueue, PriorityCount>,
            JobSystemConfig::MaximumWorkers> WorkerQueues{};
        std::array<WorkQueue, PriorityCount> MainQueues{};
        std::array<std::thread, JobSystemConfig::MaximumWorkers> Workers{};
        JobSystemConfig Config{};
        JobSystemDiagnostics Diagnostics{};
        std::thread::id OwnerThread{};
        usize WorkerCount = 0;
        usize JobCapacity = 0;
        usize NextFreeSlot = 0;
        usize NextWorker = 0;
        bool Accepting = false;
        bool StopWorkers = false;

        static thread_local State* CurrentWorkerState;
        static thread_local usize CurrentWorkerIndex;

        [[nodiscard]] JobRecord* FindJob(const JobHandle handle) noexcept
        {
            if (!handle.IsValid() || handle.Index >= JobCapacity)
            {
                return nullptr;
            }
            JobRecord& record = Jobs[handle.Index];
            return record.CurrentState != JobState::Free &&
                    record.Generation == handle.Generation
                ? &record
                : nullptr;
        }

        [[nodiscard]] const JobRecord* FindJob(const JobHandle handle) const noexcept
        {
            return const_cast<State*>(this)->FindJob(handle);
        }

        void RecordFailure() noexcept
        {
            ++Diagnostics.FailureEvents;
            TraceJobSystem("job system operation failed");
        }

        void RecordOperation() noexcept
        {
            ++Diagnostics.OperationEvents;
            TraceJobSystem("job system operation completed");
        }

        [[nodiscard]] bool QueueJob(const u32 index) noexcept
        {
            JobRecord& record = Jobs[index];
            const usize priority = static_cast<usize>(record.Priority);
            bool queued = false;
            if (record.Affinity == JobAffinity::MainThread)
            {
                queued = MainQueues[priority].PushBack(index);
            }
            else
            {
                usize worker = 0;
                if (record.Affinity == JobAffinity::PreferredWorker ||
                    record.Affinity == JobAffinity::FixedWorker)
                {
                    worker = record.RequestedWorker;
                }
                else
                {
                    worker = NextWorker++ % WorkerCount;
                }
                queued = WorkerQueues[worker][priority].PushBack(index);
            }
            if (queued)
            {
                record.CurrentState = JobState::Queued;
                WorkCondition.notify_all();
            }
            return queued;
        }

        enum class DependencyState : u8
        {
            Ready,
            Waiting,
            Cancelled
        };

        [[nodiscard]] DependencyState CheckDependencies(const JobRecord& record) const noexcept
        {
            bool waiting = false;
            for (usize index = 0; index < record.DependencyCount; ++index)
            {
                const JobRecord* dependency = FindJob(record.Dependencies[index]);
                if (dependency == nullptr || dependency->CurrentState == JobState::Cancelled)
                {
                    return DependencyState::Cancelled;
                }
                if (dependency->CurrentState != JobState::Completed)
                {
                    waiting = true;
                }
            }
            return waiting ? DependencyState::Waiting : DependencyState::Ready;
        }

        void MarkCancelled(JobRecord& record) noexcept
        {
            if (record.CurrentState == JobState::Pending ||
                record.CurrentState == JobState::Queued)
            {
                record.CurrentState = JobState::Cancelled;
                record.FinishedAt = NowNanoseconds();
                --Diagnostics.OutstandingJobs;
                ++Diagnostics.CancelledJobs;
            }
        }

        void ResolvePendingJobs() noexcept
        {
            bool changed = true;
            while (changed)
            {
                changed = false;
                for (usize index = 0; index < JobCapacity; ++index)
                {
                    JobRecord& candidate = Jobs[index];
                    if (candidate.CurrentState != JobState::Pending)
                    {
                        continue;
                    }
                    const DependencyState dependencyState = CheckDependencies(candidate);
                    if (dependencyState == DependencyState::Cancelled)
                    {
                        MarkCancelled(candidate);
                        changed = true;
                    }
                    else if (dependencyState == DependencyState::Ready)
                    {
                        if (!QueueJob(static_cast<u32>(index)))
                        {
                            MarkCancelled(candidate);
                            RecordFailure();
                        }
                        changed = true;
                    }
                }
            }
            CompletionCondition.notify_all();
        }

        [[nodiscard]] Result<JobHandle> Submit(const JobDescriptor& descriptor)
        {
            std::scoped_lock lock{Mutex};
            if (!Accepting)
            {
                RecordFailure();
                return Unexpected{Error{ErrorCode::InvalidState, "job system is not accepting jobs"}};
            }
            if (descriptor.Function == nullptr ||
                static_cast<usize>(descriptor.Priority) >= PriorityCount ||
                static_cast<usize>(descriptor.Affinity) >
                    static_cast<usize>(JobAffinity::MainThread) ||
                descriptor.Dependencies.size() > JobSystemConfig::MaximumDependenciesPerJob)
            {
                RecordFailure();
                return Unexpected{Error{ErrorCode::InvalidArgument, "invalid job descriptor"}};
            }
            if ((descriptor.Affinity == JobAffinity::PreferredWorker ||
                 descriptor.Affinity == JobAffinity::FixedWorker) &&
                descriptor.WorkerIndex >= WorkerCount)
            {
                RecordFailure();
                return Unexpected{Error{ErrorCode::InvalidArgument, "job worker affinity is out of range"}};
            }
            for (const JobHandle dependency : descriptor.Dependencies)
            {
                if (FindJob(dependency) == nullptr)
                {
                    RecordFailure();
                    return Unexpected{Error{ErrorCode::NotFound, "job dependency is stale"}};
                }
            }

            usize slot = JobCapacity;
            for (usize offset = 0; offset < JobCapacity; ++offset)
            {
                const usize candidate = (NextFreeSlot + offset) % JobCapacity;
                if (Jobs[candidate].CurrentState == JobState::Free)
                {
                    slot = candidate;
                    break;
                }
            }
            if (slot == JobCapacity)
            {
                RecordFailure();
                return Unexpected{Error{ErrorCode::OutOfMemory, "job capacity is exhausted"}};
            }

            JobRecord& record = Jobs[slot];
            record.Function = descriptor.Function;
            record.Context = descriptor.Context;
            record.DependencyCount = descriptor.Dependencies.size();
            std::copy(
                descriptor.Dependencies.begin(),
                descriptor.Dependencies.end(),
                record.Dependencies.begin());
            record.Priority = descriptor.Priority;
            record.Affinity = descriptor.Affinity;
            record.RequestedWorker = descriptor.WorkerIndex;
            record.CurrentState = JobState::Pending;
            record.ExecutionWorker = JobHandle::InvalidIndex;
            record.WaiterCount = 0;
            record.SubmittedAt = NowNanoseconds();
            record.StartedAt = 0;
            record.FinishedAt = 0;
            record.WasStolen = false;
            NextFreeSlot = (slot + 1) % JobCapacity;

            ++Diagnostics.SubmittedJobs;
            ++Diagnostics.OutstandingJobs;
            Diagnostics.PeakOutstandingJobs = std::max(
                Diagnostics.PeakOutstandingJobs,
                Diagnostics.OutstandingJobs);

            const JobHandle handle{static_cast<u32>(slot), record.Generation};
            const DependencyState dependencyState = CheckDependencies(record);
            if (dependencyState == DependencyState::Cancelled)
            {
                MarkCancelled(record);
            }
            else if (dependencyState == DependencyState::Ready && !QueueJob(handle.Index))
            {
                record.CurrentState = JobState::Free;
                --Diagnostics.OutstandingJobs;
                --Diagnostics.SubmittedJobs;
                RecordFailure();
                return Unexpected{Error{ErrorCode::OutOfMemory, "job queue capacity is exhausted"}};
            }
            RecordOperation();
            return handle;
        }

        [[nodiscard]] bool TakeOwnJob(const usize workerIndex, Execution& execution) noexcept
        {
            for (usize priority = 0; priority < PriorityCount; ++priority)
            {
                u32 index = 0;
                while (WorkerQueues[workerIndex][priority].PopFront(index))
                {
                    JobRecord& record = Jobs[index];
                    if (record.CurrentState != JobState::Queued)
                    {
                        continue;
                    }
                    record.CurrentState = JobState::Running;
                    record.ExecutionWorker = static_cast<u32>(workerIndex);
                    record.StartedAt = NowNanoseconds();
                    execution = {index, record.Generation, record.Function, record.Context};
                    ++Diagnostics.StartedJobs;
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool StealJob(const usize workerIndex, Execution& execution) noexcept
        {
            for (usize offset = 1; offset < WorkerCount; ++offset)
            {
                const usize victim = (workerIndex + offset) % WorkerCount;
                for (usize priority = 0; priority < PriorityCount; ++priority)
                {
                    WorkQueue& queue = WorkerQueues[victim][priority];
                    for (usize position = queue.Size; position > 0; --position)
                    {
                        const u32 candidate = queue.Values[position - 1];
                        JobRecord& record = Jobs[candidate];
                        if (record.CurrentState != JobState::Queued ||
                            record.Affinity == JobAffinity::FixedWorker)
                        {
                            continue;
                        }
                        u32 index = 0;
                        (void)queue.RemoveAt(position - 1, index);
                        record.CurrentState = JobState::Running;
                        record.ExecutionWorker = static_cast<u32>(workerIndex);
                        record.StartedAt = NowNanoseconds();
                        record.WasStolen = true;
                        execution = {index, record.Generation, record.Function, record.Context};
                        ++Diagnostics.StartedJobs;
                        ++Diagnostics.StolenJobs;
                        return true;
                    }
                }
            }
            return false;
        }

        [[nodiscard]] bool TakeMainJob(Execution& execution) noexcept
        {
            for (usize priority = 0; priority < PriorityCount; ++priority)
            {
                u32 index = 0;
                while (MainQueues[priority].PopFront(index))
                {
                    JobRecord& record = Jobs[index];
                    if (record.CurrentState != JobState::Queued)
                    {
                        continue;
                    }
                    record.CurrentState = JobState::Running;
                    record.ExecutionWorker = JobHandle::InvalidIndex;
                    record.StartedAt = NowNanoseconds();
                    execution = {index, record.Generation, record.Function, record.Context};
                    ++Diagnostics.StartedJobs;
                    return true;
                }
            }
            return false;
        }

        void CompleteExecution(const Execution& execution) noexcept
        {
            std::scoped_lock lock{Mutex};
            JobRecord& record = Jobs[execution.JobIndex];
            if (record.Generation != execution.Generation ||
                record.CurrentState != JobState::Running)
            {
                RecordFailure();
                return;
            }
            record.FinishedAt = NowNanoseconds();
            record.CurrentState = JobState::Completed;
            --Diagnostics.OutstandingJobs;
            ++Diagnostics.CompletedJobs;
            const u64 executionTime = record.FinishedAt - record.StartedAt;
            Diagnostics.TotalExecutionNanoseconds += executionTime;
            Diagnostics.MaximumExecutionNanoseconds = std::max(
                Diagnostics.MaximumExecutionNanoseconds,
                executionTime);
            RecordOperation();
            ResolvePendingJobs();
            CompletionCondition.notify_all();
        }

        void RunExecution(const Execution& execution) noexcept
        {
            execution.Function(execution.Context);
            CompleteExecution(execution);
        }

        [[nodiscard]] bool ExecuteOneWorker(const usize workerIndex) noexcept
        {
            Execution execution;
            {
                std::scoped_lock lock{Mutex};
                if (!TakeOwnJob(workerIndex, execution) && !StealJob(workerIndex, execution))
                {
                    return false;
                }
            }
            RunExecution(execution);
            return true;
        }

        [[nodiscard]] bool ExecuteOneMain() noexcept
        {
            Execution execution;
            {
                std::scoped_lock lock{Mutex};
                if (!TakeMainJob(execution))
                {
                    return false;
                }
            }
            RunExecution(execution);
            return true;
        }

        [[nodiscard]] bool HasWorkerWork() const noexcept
        {
            for (usize worker = 0; worker < WorkerCount; ++worker)
            {
                for (usize priority = 0; priority < PriorityCount; ++priority)
                {
                    if (WorkerQueues[worker][priority].Size != 0)
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        void WorkerMain(const usize workerIndex) noexcept
        {
            CurrentWorkerState = this;
            CurrentWorkerIndex = workerIndex;
            for (;;)
            {
                if (ExecuteOneWorker(workerIndex))
                {
                    continue;
                }
                std::unique_lock lock{Mutex};
                WorkCondition.wait(lock, [this]
                {
                    return StopWorkers || HasWorkerWork();
                });
                if (StopWorkers && !HasWorkerWork())
                {
                    CurrentWorkerState = nullptr;
                    CurrentWorkerIndex = 0;
                    return;
                }
            }
        }

        [[nodiscard]] Result<void> WaitForJob(
            const JobHandle handle,
            const Milliseconds timeout)
        {
            if (!IsValidTimeout(timeout))
            {
                std::scoped_lock lock{Mutex};
                RecordFailure();
                return Unexpected{Error{ErrorCode::InvalidArgument, "invalid job wait timeout"}};
            }
            const bool infinite = timeout == Milliseconds::max();
            const auto deadline = infinite
                ? std::chrono::steady_clock::time_point::max()
                : std::chrono::steady_clock::now() + timeout;

            std::unique_lock lock{Mutex};
            JobRecord* record = FindJob(handle);
            if (record == nullptr)
            {
                RecordFailure();
                return Unexpected{Error{ErrorCode::NotFound, "job handle is stale"}};
            }
            ++record->WaiterCount;
            ++Diagnostics.WaitCalls;
            while (!IsTerminal(record->CurrentState))
            {
                if (!infinite && std::chrono::steady_clock::now() >= deadline)
                {
                    --record->WaiterCount;
                    ++Diagnostics.WaitTimeouts;
                    RecordFailure();
                    return Unexpected{Error{ErrorCode::Timeout, "job wait timed out"}};
                }

                const bool isWorker = CurrentWorkerState == this;
                const bool isOwner = std::this_thread::get_id() == OwnerThread;
                lock.unlock();
                const bool helped = isWorker
                    ? ExecuteOneWorker(CurrentWorkerIndex)
                    : isOwner && ExecuteOneMain();
                lock.lock();
                record = FindJob(handle);
                if (record == nullptr)
                {
                    RecordFailure();
                    return Unexpected{Error{ErrorCode::NotFound, "job handle became stale"}};
                }
                if (!helped && !IsTerminal(record->CurrentState))
                {
                    if (infinite)
                    {
                        CompletionCondition.wait_for(lock, Milliseconds{1});
                    }
                    else
                    {
                        CompletionCondition.wait_until(lock, deadline);
                    }
                }
            }
            --record->WaiterCount;
            if (record->CurrentState == JobState::Cancelled)
            {
                return Unexpected{Error{ErrorCode::Cancelled, "job was cancelled"}};
            }
            return {};
        }

        [[nodiscard]] Result<JobBarrier> CreateBarrier(const Span<const JobHandle> handles)
        {
            std::scoped_lock lock{Mutex};
            if (handles.size() > JobSystemConfig::MaximumBarrierJobs)
            {
                RecordFailure();
                return Unexpected{Error{ErrorCode::InvalidArgument, "job barrier is too large"}};
            }
            JobBarrier barrier;
            for (const JobHandle handle : handles)
            {
                if (FindJob(handle) == nullptr)
                {
                    RecordFailure();
                    return Unexpected{Error{ErrorCode::NotFound, "job barrier contains a stale handle"}};
                }
                barrier.m_jobs[barrier.m_count++] = handle;
            }
            RecordOperation();
            return barrier;
        }

        [[nodiscard]] Result<void> Cancel(const JobHandle handle)
        {
            std::scoped_lock lock{Mutex};
            JobRecord* record = FindJob(handle);
            if (record == nullptr)
            {
                RecordFailure();
                return Unexpected{Error{ErrorCode::NotFound, "job handle is stale"}};
            }
            if (record->CurrentState != JobState::Pending &&
                record->CurrentState != JobState::Queued)
            {
                RecordFailure();
                return Unexpected{Error{ErrorCode::InvalidState, "job cannot be cancelled in its current state"}};
            }
            MarkCancelled(*record);
            RecordOperation();
            ResolvePendingJobs();
            WorkCondition.notify_all();
            return {};
        }

        [[nodiscard]] Result<void> Release(const JobHandle handle)
        {
            std::scoped_lock lock{Mutex};
            JobRecord* record = FindJob(handle);
            if (record == nullptr)
            {
                RecordFailure();
                return Unexpected{Error{ErrorCode::NotFound, "job handle is stale"}};
            }
            if (!IsTerminal(record->CurrentState) || record->WaiterCount != 0)
            {
                RecordFailure();
                return Unexpected{Error{ErrorCode::InvalidState, "job is still active or observed"}};
            }
            for (usize index = 0; index < JobCapacity; ++index)
            {
                const JobRecord& candidate = Jobs[index];
                if (candidate.CurrentState != JobState::Pending)
                {
                    continue;
                }
                for (usize dependency = 0; dependency < candidate.DependencyCount; ++dependency)
                {
                    if (candidate.Dependencies[dependency] == handle)
                    {
                        RecordFailure();
                        return Unexpected{Error{ErrorCode::InvalidState, "job is retained by a dependency graph"}};
                    }
                }
            }

            const u32 nextGeneration = record->Generation == std::numeric_limits<u32>::max()
                ? 1
                : record->Generation + 1;
            *record = JobRecord{};
            record->Generation = nextGeneration;
            ++Diagnostics.ReleasedJobs;
            RecordOperation();
            return {};
        }

        [[nodiscard]] Result<JobProfile> GetProfile(const JobHandle handle)
        {
            std::scoped_lock lock{Mutex};
            const JobRecord* record = FindJob(handle);
            if (record == nullptr)
            {
                RecordFailure();
                return Unexpected{Error{ErrorCode::NotFound, "job handle is stale"}};
            }
            const u64 queueTime = record->StartedAt >= record->SubmittedAt
                ? record->StartedAt - record->SubmittedAt
                : 0;
            const u64 executionTime = record->FinishedAt >= record->StartedAt &&
                    record->StartedAt != 0
                ? record->FinishedAt - record->StartedAt
                : 0;
            return JobProfile{
                record->CurrentState,
                record->ExecutionWorker,
                queueTime,
                executionTime,
                record->WasStolen};
        }

        [[nodiscard]] JobSystemDiagnostics GetDiagnostics() const noexcept
        {
            std::scoped_lock lock{Mutex};
            return Diagnostics;
        }

        void CancelAllPending() noexcept
        {
            std::scoped_lock lock{Mutex};
            for (usize index = 0; index < JobCapacity; ++index)
            {
                MarkCancelled(Jobs[index]);
            }
            ResolvePendingJobs();
            WorkCondition.notify_all();
        }
    };

    thread_local JobSystem::State* JobSystem::State::CurrentWorkerState = nullptr;
    thread_local usize JobSystem::State::CurrentWorkerIndex = 0;

    JobSystem::StateLease::StateLease(const JobSystem& owner) noexcept
        : m_owner(&owner),
          m_state(owner.AcquireState())
    {
    }

    JobSystem::StateLease::~StateLease()
    {
        if (m_state != nullptr)
        {
            m_owner->ReleaseState();
        }
    }

    JobSystem::~JobSystem()
    {
        Shutdown();
    }

    JobSystem::State* JobSystem::AcquireState() const noexcept
    {
        std::scoped_lock lock{m_lifecycleMutex};
        if (m_state == nullptr || m_isShuttingDown)
        {
            return nullptr;
        }
        ++m_activeCalls;
        return m_state;
    }

    void JobSystem::ReleaseState() const noexcept
    {
        std::scoped_lock lock{m_lifecycleMutex};
        --m_activeCalls;
        if (m_activeCalls == 0)
        {
            m_lifecycleCondition.notify_all();
        }
    }

    void JobSystem::RecordUnavailableFailure() const noexcept
    {
        std::scoped_lock lock{m_lifecycleMutex};
        ++m_retainedDiagnostics.FailureEvents;
    }

    Result<void> JobSystem::Initialise(const JobSystemConfig& config)
    {
        std::unique_lock lifecycleLock{m_lifecycleMutex};
        if (m_state != nullptr || m_isShuttingDown)
        {
            ++m_retainedDiagnostics.FailureEvents;
            return Unexpected{Error{ErrorCode::InvalidState, "job system is already initialised"}};
        }
        if (config.JobCapacity == 0 ||
            config.JobCapacity > JobSystemConfig::MaximumJobs ||
            config.WorkerCount > JobSystemConfig::MaximumWorkers)
        {
            ++m_retainedDiagnostics.FailureEvents;
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid job system capacity"}};
        }

        usize workerCount = config.WorkerCount;
        if (workerCount == 0)
        {
            const usize hardwareThreads = std::thread::hardware_concurrency();
            workerCount = hardwareThreads > 1 ? hardwareThreads - 1 : 1;
            workerCount = std::min(workerCount, JobSystemConfig::MaximumWorkers);
        }

        IAllocator& allocator = config.Allocator != nullptr
            ? *config.Allocator
            : GetSystemAllocator();
        Result<Allocation> allocationResult = allocator.Allocate({
            sizeof(State),
            alignof(State),
            MemoryTag::Persistent});
        if (!allocationResult)
        {
            ++m_retainedDiagnostics.FailureEvents;
            return Unexpected{allocationResult.ErrorValue()};
        }

        Allocation allocation = allocationResult.Value();
        State* const state = std::construct_at(static_cast<State*>(allocation.Data));
        state->Config = config;
        state->Config.WorkerCount = workerCount;
        state->Config.Allocator = &allocator;
        state->WorkerCount = workerCount;
        state->JobCapacity = config.JobCapacity;
        state->OwnerThread = std::this_thread::get_id();
        state->Accepting = true;
        state->Diagnostics.StartupEvents = 1;
        state->Diagnostics.PersistentBytes = allocation.Size;
        state->Diagnostics.WorkerCount = workerCount;
        state->Diagnostics.JobCapacity = config.JobCapacity;
        state->Diagnostics.IsInitialised = true;
        state->Diagnostics.IsAcceptingJobs = true;

        try
        {
            for (usize worker = 0; worker < workerCount; ++worker)
            {
                state->Workers[worker] = std::thread{[state, worker]
                {
                    state->WorkerMain(worker);
                }};
            }
        }
        catch (...)
        {
            {
                std::scoped_lock stateLock{state->Mutex};
                state->StopWorkers = true;
                state->Accepting = false;
            }
            state->WorkCondition.notify_all();
            for (std::thread& worker : state->Workers)
            {
                if (worker.joinable())
                {
                    worker.join();
                }
            }
            std::destroy_at(state);
            (void)allocator.Deallocate(allocation);
            ++m_retainedDiagnostics.FailureEvents;
            return Unexpected{Error{ErrorCode::Internal, "job worker creation failed"}};
        }

        m_allocator = &allocator;
        m_stateAllocation = allocation;
        m_state = state;
        m_retainedDiagnostics = state->Diagnostics;
        TraceJobSystem("job system initialised");
        return {};
    }

    Result<JobHandle> JobSystem::Submit(const JobDescriptor& descriptor)
    {
        StateLease lease{*this};
        if (lease.Get() == nullptr)
        {
            RecordUnavailableFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "job system is not initialised"}};
        }
        return lease.Get()->Submit(descriptor);
    }

    Result<JobBarrier> JobSystem::CreateBarrier(const Span<const JobHandle> jobs)
    {
        StateLease lease{*this};
        if (lease.Get() == nullptr)
        {
            RecordUnavailableFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "job system is not initialised"}};
        }
        return lease.Get()->CreateBarrier(jobs);
    }

    Result<void> JobSystem::Wait(const JobHandle job, const Milliseconds timeout)
    {
        StateLease lease{*this};
        if (lease.Get() == nullptr)
        {
            RecordUnavailableFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "job system is not initialised"}};
        }
        return lease.Get()->WaitForJob(job, timeout);
    }

    Result<void> JobSystem::Wait(
        const JobBarrier& barrier,
        const Milliseconds timeout)
    {
        if (!IsValidTimeout(timeout))
        {
            RecordUnavailableFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid barrier timeout"}};
        }
        StateLease lease{*this};
        if (lease.Get() == nullptr)
        {
            RecordUnavailableFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "job system is not initialised"}};
        }

        const bool infinite = timeout == Milliseconds::max();
        const auto deadline = infinite
            ? std::chrono::steady_clock::time_point::max()
            : std::chrono::steady_clock::now() + timeout;
        for (usize index = 0; index < barrier.m_count; ++index)
        {
            Milliseconds remaining = Milliseconds::max();
            if (!infinite)
            {
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline)
                {
                    return Unexpected{Error{ErrorCode::Timeout, "job barrier wait timed out"}};
                }
                remaining = std::chrono::duration_cast<Milliseconds>(deadline - now);
                if (remaining.count() == 0)
                {
                    remaining = Milliseconds{1};
                }
            }
            SKEIN_TRY(lease.Get()->WaitForJob(barrier.m_jobs[index], remaining));
        }
        return {};
    }

    Result<void> JobSystem::Cancel(const JobHandle job)
    {
        StateLease lease{*this};
        if (lease.Get() == nullptr)
        {
            RecordUnavailableFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "job system is not initialised"}};
        }
        return lease.Get()->Cancel(job);
    }

    Result<void> JobSystem::Release(const JobHandle job)
    {
        StateLease lease{*this};
        if (lease.Get() == nullptr)
        {
            RecordUnavailableFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "job system is not initialised"}};
        }
        return lease.Get()->Release(job);
    }

    Result<JobProfile> JobSystem::GetProfile(const JobHandle job) const
    {
        StateLease lease{*this};
        if (lease.Get() == nullptr)
        {
            RecordUnavailableFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "job system is not initialised"}};
        }
        return lease.Get()->GetProfile(job);
    }

    Result<void> JobSystem::ParallelFor(
        const usize itemCount,
        const usize minimumBatchSize,
        const ParallelForFunction function,
        void* const context,
        const JobPriority priority)
    {
        if (minimumBatchSize == 0 || function == nullptr ||
            static_cast<usize>(priority) >= PriorityCount)
        {
            RecordUnavailableFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid parallel-for descriptor"}};
        }
        if (itemCount == 0)
        {
            return {};
        }
        StateLease lease{*this};
        State* const state = lease.Get();
        if (state == nullptr)
        {
            RecordUnavailableFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "job system is not initialised"}};
        }

        const usize desiredBatches = 1 + (itemCount - 1) / minimumBatchSize;
        const usize batchCount = std::min(
            desiredBatches,
            JobSystemConfig::MaximumParallelJobs);
        const usize batchSize = 1 + (itemCount - 1) / batchCount;
        std::array<ParallelForContext, JobSystemConfig::MaximumParallelJobs> contexts{};
        std::array<JobHandle, JobSystemConfig::MaximumParallelJobs> handles{};
        usize submitted = 0;

        {
            std::scoped_lock lock{state->Mutex};
            ++state->Diagnostics.ParallelForCalls;
            state->Diagnostics.ParallelForBatches += batchCount;
        }
        for (usize batch = 0; batch < batchCount; ++batch)
        {
            const usize begin = batch * batchSize;
            const usize end = std::min(begin + batchSize, itemCount);
            if (begin == end)
            {
                break;
            }
            contexts[batch] = {function, context, begin, end};
            Result<JobHandle> submitResult = state->Submit({
                RunParallelForBatch,
                &contexts[batch],
                {},
                priority,
                JobAffinity::AnyWorker,
                0});
            if (!submitResult)
            {
                for (usize index = 0; index < submitted; ++index)
                {
                    (void)state->Cancel(handles[index]);
                    (void)state->WaitForJob(handles[index], Milliseconds::max());
                    (void)state->Release(handles[index]);
                }
                return Unexpected{submitResult.ErrorValue()};
            }
            handles[submitted++] = submitResult.Value();
        }

        Error firstError;
        for (usize index = 0; index < submitted; ++index)
        {
            Result<void> waitResult = state->WaitForJob(handles[index], Milliseconds::max());
            if (!waitResult && !firstError)
            {
                firstError = waitResult.ErrorValue();
            }
        }
        for (usize index = 0; index < submitted; ++index)
        {
            Result<void> releaseResult = state->Release(handles[index]);
            if (!releaseResult && !firstError)
            {
                firstError = releaseResult.ErrorValue();
            }
        }
        if (firstError)
        {
            return Unexpected{firstError};
        }
        return {};
    }

    Result<usize> JobSystem::Pump(const usize maximumJobs)
    {
        StateLease lease{*this};
        State* const state = lease.Get();
        if (state == nullptr)
        {
            RecordUnavailableFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "job system is not initialised"}};
        }
        if (std::this_thread::get_id() != state->OwnerThread)
        {
            std::scoped_lock lock{state->Mutex};
            state->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "main-thread jobs must be pumped by the owner"}};
        }
        usize executed = 0;
        while (executed < maximumJobs && state->ExecuteOneMain())
        {
            ++executed;
        }
        return executed;
    }

    void JobSystem::Shutdown(const JobShutdownMode mode) noexcept
    {
        State* state = nullptr;
        {
            std::scoped_lock lifecycleLock{m_lifecycleMutex};
            if (m_state == nullptr || m_isShuttingDown)
            {
                return;
            }
            m_isShuttingDown = true;
            state = m_state;
        }

        {
            std::scoped_lock lock{state->Mutex};
            state->Accepting = false;
            state->Diagnostics.IsAcceptingJobs = false;
        }
        const bool isOwnerThread = std::this_thread::get_id() == state->OwnerThread;
        const bool mustCancelPending = mode == JobShutdownMode::CancelPending || !isOwnerThread;
        if (!isOwnerThread)
        {
            std::scoped_lock lock{state->Mutex};
            state->RecordFailure();
        }
        if (mustCancelPending)
        {
            state->CancelAllPending();
        }

        for (;;)
        {
            {
                std::unique_lock lock{state->Mutex};
                if (state->Diagnostics.OutstandingJobs == 0)
                {
                    break;
                }
                lock.unlock();
                const bool helped = isOwnerThread && state->ExecuteOneMain();
                lock.lock();
                if (!helped && state->Diagnostics.OutstandingJobs != 0)
                {
                    state->CompletionCondition.wait_for(lock, Milliseconds{1});
                }
            }
        }

        {
            std::scoped_lock lock{state->Mutex};
            state->StopWorkers = true;
        }
        state->WorkCondition.notify_all();
        for (usize worker = 0; worker < state->WorkerCount; ++worker)
        {
            if (state->Workers[worker].joinable())
            {
                state->Workers[worker].join();
            }
        }

        {
            std::scoped_lock lock{state->Mutex};
            ++state->Diagnostics.ShutdownEvents;
            state->Diagnostics.IsInitialised = false;
            state->Diagnostics.IsAcceptingJobs = false;
        }
        TraceJobSystem("job system shut down");

        std::unique_lock lifecycleLock{m_lifecycleMutex};
        m_lifecycleCondition.wait(lifecycleLock, [this]
        {
            return m_activeCalls == 0;
        });
        m_retainedDiagnostics = state->GetDiagnostics();
        std::destroy_at(state);
        Allocation allocation = m_stateAllocation;
        IAllocator* const allocator = m_allocator;
        m_state = nullptr;
        m_allocator = nullptr;
        m_stateAllocation = {};
        m_isShuttingDown = false;
        lifecycleLock.unlock();
        if (allocator != nullptr)
        {
            (void)allocator->Deallocate(allocation);
        }
    }

    JobSystemDiagnostics JobSystem::GetDiagnostics() const noexcept
    {
        StateLease lease{*this};
        if (lease.Get() != nullptr)
        {
            return lease.Get()->GetDiagnostics();
        }
        std::scoped_lock lock{m_lifecycleMutex};
        return m_retainedDiagnostics;
    }
}

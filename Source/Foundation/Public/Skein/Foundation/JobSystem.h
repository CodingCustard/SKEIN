#pragma once

#include <Skein/Foundation/Memory.h>
#include <Skein/Foundation/Result.h>

#include <array>
#include <condition_variable>
#include <limits>
#include <mutex>

namespace Skein
{
    enum class JobPriority : u8
    {
        High,
        Normal,
        Low,
        Count
    };

    enum class JobAffinity : u8
    {
        AnyWorker,
        PreferredWorker,
        FixedWorker,
        MainThread
    };

    enum class JobState : u8
    {
        Free,
        Pending,
        Queued,
        Running,
        Completed,
        Cancelled
    };

    enum class JobShutdownMode : u8
    {
        Drain,
        CancelPending
    };

    struct JobHandle final
    {
        static constexpr u32 InvalidIndex = std::numeric_limits<u32>::max();

        u32 Index = InvalidIndex;
        u32 Generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return Index != InvalidIndex && Generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const JobHandle&,
            const JobHandle&) noexcept = default;
    };

    using JobFunction = void (*)(void* context) noexcept;
    using ParallelForFunction = void (*)(
        void* context,
        usize beginIndex,
        usize endIndex) noexcept;

    struct JobDescriptor final
    {
        JobFunction Function = nullptr;
        void* Context = nullptr;
        Span<const JobHandle> Dependencies{};
        JobPriority Priority = JobPriority::Normal;
        JobAffinity Affinity = JobAffinity::AnyWorker;
        u32 WorkerIndex = 0;
    };

    struct JobSystemConfig final
    {
        static constexpr usize MaximumWorkers = 16;
        static constexpr usize MaximumJobs = 1024;
        static constexpr usize MaximumDependenciesPerJob = 8;
        static constexpr usize MaximumBarrierJobs = 256;
        static constexpr usize MaximumParallelJobs = 256;

        usize WorkerCount = 0;
        usize JobCapacity = MaximumJobs;
        IAllocator* Allocator = nullptr;
    };

    struct JobProfile final
    {
        JobState State = JobState::Free;
        u32 WorkerIndex = JobHandle::InvalidIndex;
        u64 QueueNanoseconds = 0;
        u64 ExecutionNanoseconds = 0;
        bool WasStolen = false;
    };

    struct JobSystemDiagnostics final
    {
        u64 StartupEvents = 0;
        u64 OperationEvents = 0;
        u64 FailureEvents = 0;
        u64 ShutdownEvents = 0;
        u64 SubmittedJobs = 0;
        u64 StartedJobs = 0;
        u64 CompletedJobs = 0;
        u64 CancelledJobs = 0;
        u64 ReleasedJobs = 0;
        u64 StolenJobs = 0;
        u64 WaitCalls = 0;
        u64 WaitTimeouts = 0;
        u64 ParallelForCalls = 0;
        u64 ParallelForBatches = 0;
        u64 TotalExecutionNanoseconds = 0;
        u64 MaximumExecutionNanoseconds = 0;
        usize PersistentBytes = 0;
        usize OutstandingJobs = 0;
        usize PeakOutstandingJobs = 0;
        usize WorkerCount = 0;
        usize JobCapacity = 0;
        bool IsInitialised = false;
        bool IsAcceptingJobs = false;
    };

    class JobBarrier final
    {
    public:
        [[nodiscard]] usize Size() const noexcept { return m_count; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_count == 0; }

    private:
        friend class JobSystem;

        std::array<JobHandle, JobSystemConfig::MaximumBarrierJobs> m_jobs{};
        usize m_count = 0;
    };

    class JobSystem final
    {
    public:
        JobSystem() noexcept = default;
        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;
        ~JobSystem();

        [[nodiscard]] Result<void> Initialise(const JobSystemConfig& config = {});
        [[nodiscard]] Result<JobHandle> Submit(const JobDescriptor& descriptor);
        [[nodiscard]] Result<JobBarrier> CreateBarrier(Span<const JobHandle> jobs);
        [[nodiscard]] Result<void> Wait(
            JobHandle job,
            Milliseconds timeout = Milliseconds::max());
        [[nodiscard]] Result<void> Wait(
            const JobBarrier& barrier,
            Milliseconds timeout = Milliseconds::max());
        [[nodiscard]] Result<void> Cancel(JobHandle job);
        [[nodiscard]] Result<void> Release(JobHandle job);
        [[nodiscard]] Result<JobProfile> GetProfile(JobHandle job) const;

        [[nodiscard]] Result<void> ParallelFor(
            usize itemCount,
            usize minimumBatchSize,
            ParallelForFunction function,
            void* context = nullptr,
            JobPriority priority = JobPriority::Normal);

        [[nodiscard]] Result<usize> Pump(
            usize maximumJobs = std::numeric_limits<usize>::max());
        void Shutdown(JobShutdownMode mode = JobShutdownMode::Drain) noexcept;

        [[nodiscard]] JobSystemDiagnostics GetDiagnostics() const noexcept;

    private:
        struct State;

        class StateLease final
        {
        public:
            explicit StateLease(const JobSystem& owner) noexcept;
            StateLease(const StateLease&) = delete;
            StateLease& operator=(const StateLease&) = delete;
            ~StateLease();

            [[nodiscard]] State* Get() const noexcept { return m_state; }

        private:
            const JobSystem* m_owner = nullptr;
            State* m_state = nullptr;
        };

        [[nodiscard]] State* AcquireState() const noexcept;
        void ReleaseState() const noexcept;
        void RecordUnavailableFailure() const noexcept;

        mutable std::mutex m_lifecycleMutex;
        mutable std::condition_variable m_lifecycleCondition;
        mutable usize m_activeCalls = 0;
        State* m_state = nullptr;
        IAllocator* m_allocator = nullptr;
        Allocation m_stateAllocation{};
        mutable JobSystemDiagnostics m_retainedDiagnostics{};
        bool m_isShuttingDown = false;
    };
}

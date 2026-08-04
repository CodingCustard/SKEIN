#include <Skein/Foundation/JobSystem.h>

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

namespace
{
    struct BlockingContext final
    {
        std::atomic<bool> Started = false;
        std::atomic<bool> Release = false;
    };

    void BlockJob(void* const context) noexcept
    {
        auto& blocking = *static_cast<BlockingContext*>(context);
        blocking.Started.store(true, std::memory_order_release);
        while (!blocking.Release.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    }

    struct OrderedContext final
    {
        std::array<Skein::u32, 2>* Values = nullptr;
        std::atomic<Skein::usize>* Position = nullptr;
        Skein::u32 Value = 0;
    };

    void RecordOrder(void* const context) noexcept
    {
        auto& ordered = *static_cast<OrderedContext*>(context);
        const Skein::usize position = ordered.Position->fetch_add(1, std::memory_order_relaxed);
        (*ordered.Values)[position] = ordered.Value;
    }

    void Increment(void* const context) noexcept
    {
        static_cast<std::atomic<Skein::u32>*>(context)->fetch_add(1, std::memory_order_relaxed);
    }

    struct NestedWaitContext final
    {
        Skein::JobSystem* Jobs = nullptr;
        std::atomic<Skein::u32> ChildRuns = 0;
        std::atomic<bool> Succeeded = false;
    };

    void SubmitAndWait(void* const context) noexcept
    {
        auto& nested = *static_cast<NestedWaitContext*>(context);
        Skein::Result<Skein::JobHandle> child = nested.Jobs->Submit({
            Increment,
            &nested.ChildRuns});
        if (!child)
        {
            return;
        }
        const bool succeeded = nested.Jobs->Wait(child.Value()).HasValue() &&
            nested.Jobs->Release(child.Value()).HasValue();
        nested.Succeeded.store(succeeded, std::memory_order_release);
    }

    struct DependencyContext final
    {
        std::atomic<Skein::u32>* Value = nullptr;
        Skein::u32 Expected = 0;
        Skein::u32 Replacement = 0;
        std::atomic<bool>* Succeeded = nullptr;
    };

    void AdvanceDependency(void* const context) noexcept
    {
        auto& dependency = *static_cast<DependencyContext*>(context);
        Skein::u32 expected = dependency.Expected;
        const bool exchanged = dependency.Value->compare_exchange_strong(
            expected,
            dependency.Replacement,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
        dependency.Succeeded->store(exchanged, std::memory_order_release);
    }

    struct MainThreadContext final
    {
        std::thread::id ExecutedOn{};
    };

    void RecordThread(void* const context) noexcept
    {
        static_cast<MainThreadContext*>(context)->ExecutedOn = std::this_thread::get_id();
    }

    void SumRange(
        void* const context,
        const Skein::usize begin,
        const Skein::usize end) noexcept
    {
        auto& total = *static_cast<std::atomic<Skein::u64>*>(context);
        Skein::u64 local = 0;
        for (Skein::usize index = begin; index < end; ++index)
        {
            local += index;
        }
        total.fetch_add(local, std::memory_order_relaxed);
    }

    [[nodiscard]] bool WaitUntilStarted(BlockingContext& context)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (!context.Started.load(std::memory_order_acquire))
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }
}

int main()
{
    using namespace Skein;

    JobSystem invalidSystem;
    JobSystemConfig invalidConfig;
    invalidConfig.JobCapacity = 0;
    if (invalidSystem.Initialise(invalidConfig))
    {
        return 1;
    }

    JobSystem prioritySystem;
    MemoryArchitectureService jobMemory;
    MemoryArchitectureConfig memoryConfig;
    memoryConfig.TrackedAllocationCapacity = 8;
    JobSystemConfig priorityConfig;
    priorityConfig.WorkerCount = 1;
    priorityConfig.JobCapacity = 8;
    priorityConfig.Allocator = &jobMemory;
    if (!jobMemory.Initialise(memoryConfig) ||
        !prioritySystem.Initialise(priorityConfig) || prioritySystem.Initialise(priorityConfig))
    {
        return 2;
    }
    const MemoryDiagnostics startupMemory = jobMemory.GetDiagnostics();
    if (startupMemory.Tags[static_cast<usize>(MemoryTag::Persistent)].ActiveAllocations != 1 ||
        startupMemory.Tags[static_cast<usize>(MemoryTag::Persistent)].AllocationCount != 1)
    {
        return 26;
    }

    BlockingContext priorityBlocker;
    Result<JobHandle> priorityBlockerResult = prioritySystem.Submit({
        BlockJob,
        &priorityBlocker,
        {},
        JobPriority::Normal,
        JobAffinity::FixedWorker,
        0});
    if (!priorityBlockerResult || !WaitUntilStarted(priorityBlocker))
    {
        return 3;
    }

    std::array<u32, 2> order{};
    std::atomic<usize> orderPosition = 0;
    OrderedContext lowContext{&order, &orderPosition, 1};
    OrderedContext highContext{&order, &orderPosition, 2};
    Result<JobHandle> lowResult = prioritySystem.Submit({
        RecordOrder,
        &lowContext,
        {},
        JobPriority::Low});
    Result<JobHandle> highResult = prioritySystem.Submit({
        RecordOrder,
        &highContext,
        {},
        JobPriority::High});
    Result<void> timeoutResult = prioritySystem.Wait(
        priorityBlockerResult.Value(),
        Milliseconds{1});
    if (!lowResult || !highResult || timeoutResult ||
        timeoutResult.ErrorValue().Code() != ErrorCode::Timeout)
    {
        return 4;
    }
    priorityBlocker.Release.store(true, std::memory_order_release);
    const std::array priorityJobs{
        priorityBlockerResult.Value(),
        lowResult.Value(),
        highResult.Value()};
    Result<JobBarrier> priorityBarrier = prioritySystem.CreateBarrier(priorityJobs);
    if (!priorityBarrier || !prioritySystem.Wait(priorityBarrier.Value()) ||
        order[0] != 2 || order[1] != 1)
    {
        return 5;
    }
    for (const JobHandle handle : priorityJobs)
    {
        if (!prioritySystem.Release(handle))
        {
            return 6;
        }
    }
    NestedWaitContext nestedWait{&prioritySystem};
    Result<JobHandle> nestedParent = prioritySystem.Submit({SubmitAndWait, &nestedWait});
    if (!nestedParent || !prioritySystem.Wait(nestedParent.Value()) ||
        !nestedWait.Succeeded.load(std::memory_order_acquire) ||
        nestedWait.ChildRuns.load(std::memory_order_relaxed) != 1 ||
        !prioritySystem.Release(nestedParent.Value()))
    {
        return 25;
    }
    const MemoryDiagnostics operationMemory = jobMemory.GetDiagnostics();
    if (operationMemory.Tags[static_cast<usize>(MemoryTag::Persistent)].AllocationCount != 1)
    {
        return 27;
    }
    prioritySystem.Shutdown();
    const MemoryDiagnostics releasedMemory = jobMemory.GetDiagnostics();
    if (releasedMemory.Tags[static_cast<usize>(MemoryTag::Persistent)].ActiveAllocations != 0)
    {
        return 28;
    }
    jobMemory.Shutdown();

    JobSystem jobs;
    JobSystemConfig config;
    config.WorkerCount = 4;
    config.JobCapacity = 128;
    if (!jobs.Initialise(config) || jobs.Submit({}))
    {
        return 7;
    }

    BlockingContext workerZeroBlocker;
    Result<JobHandle> blockerResult = jobs.Submit({
        BlockJob,
        &workerZeroBlocker,
        {},
        JobPriority::Normal,
        JobAffinity::FixedWorker,
        0});
    if (!blockerResult || !WaitUntilStarted(workerZeroBlocker))
    {
        return 8;
    }

    Result<JobHandle> cancelledResult = jobs.Submit({
        Increment,
        nullptr,
        Span<const JobHandle>{&blockerResult.Value(), 1}});
    if (!cancelledResult || !jobs.Cancel(cancelledResult.Value()))
    {
        return 9;
    }
    Result<void> cancelledWait = jobs.Wait(cancelledResult.Value());
    if (cancelledWait || cancelledWait.ErrorValue().Code() != ErrorCode::Cancelled ||
        !jobs.Release(cancelledResult.Value()))
    {
        return 10;
    }

    std::atomic<u32> stolenWorkCount = 0;
    std::array<JobHandle, 48> stealHandles{};
    for (JobHandle& handle : stealHandles)
    {
        Result<JobHandle> result = jobs.Submit({
            Increment,
            &stolenWorkCount,
            {},
            JobPriority::Normal,
            JobAffinity::PreferredWorker,
            0});
        if (!result)
        {
            return 11;
        }
        handle = result.Value();
    }
    Result<JobBarrier> stealBarrier = jobs.CreateBarrier(stealHandles);
    if (!stealBarrier || !jobs.Wait(stealBarrier.Value()) ||
        stolenWorkCount.load(std::memory_order_relaxed) != stealHandles.size())
    {
        return 12;
    }
    for (const JobHandle handle : stealHandles)
    {
        Result<JobProfile> profile = jobs.GetProfile(handle);
        if (!profile || profile.Value().State != JobState::Completed ||
            !jobs.Release(handle))
        {
            return 13;
        }
    }

    std::atomic<u32> graphValue = 0;
    std::atomic<bool> parentSucceeded = false;
    std::atomic<bool> childSucceeded = false;
    DependencyContext parentContext{&graphValue, 0, 1, &parentSucceeded};
    DependencyContext childContext{&graphValue, 1, 2, &childSucceeded};
    Result<JobHandle> parentResult = jobs.Submit({AdvanceDependency, &parentContext});
    if (!parentResult)
    {
        return 14;
    }
    const JobHandle parentHandle = parentResult.Value();
    Result<JobHandle> childResult = jobs.Submit({
        AdvanceDependency,
        &childContext,
        Span<const JobHandle>{&parentHandle, 1},
        JobPriority::High});
    if (!childResult || !jobs.Wait(childResult.Value()) ||
        !parentSucceeded.load(std::memory_order_acquire) ||
        !childSucceeded.load(std::memory_order_acquire) ||
        graphValue.load(std::memory_order_acquire) != 2 ||
        !jobs.Release(parentHandle) || !jobs.Release(childResult.Value()))
    {
        return 15;
    }

    std::atomic<u32> fixedWorkerCount = 0;
    Result<JobHandle> fixedResult = jobs.Submit({
        Increment,
        &fixedWorkerCount,
        {},
        JobPriority::Normal,
        JobAffinity::FixedWorker,
        2});
    if (!fixedResult || !jobs.Wait(fixedResult.Value()))
    {
        return 16;
    }
    Result<JobProfile> fixedProfile = jobs.GetProfile(fixedResult.Value());
    if (!fixedProfile || fixedProfile.Value().WorkerIndex != 2 ||
        fixedWorkerCount.load(std::memory_order_relaxed) != 1 ||
        !jobs.Release(fixedResult.Value()))
    {
        return 17;
    }

    MainThreadContext mainContext;
    const std::thread::id ownerThread = std::this_thread::get_id();
    Result<JobHandle> mainResult = jobs.Submit({
        RecordThread,
        &mainContext,
        {},
        JobPriority::Normal,
        JobAffinity::MainThread});
    if (!mainResult || !jobs.Wait(mainResult.Value()) ||
        mainContext.ExecutedOn != ownerThread)
    {
        return 18;
    }
    Result<JobProfile> mainProfile = jobs.GetProfile(mainResult.Value());
    const JobHandle staleMainHandle = mainResult.Value();
    if (!mainProfile || mainProfile.Value().WorkerIndex != JobHandle::InvalidIndex ||
        !jobs.Release(staleMainHandle) || jobs.Wait(staleMainHandle, Milliseconds{0}))
    {
        return 19;
    }

    std::atomic<u64> parallelTotal = 0;
    if (!jobs.ParallelFor(1000, 17, SumRange, &parallelTotal, JobPriority::High) ||
        parallelTotal.load(std::memory_order_relaxed) != 499500)
    {
        return 20;
    }

    workerZeroBlocker.Release.store(true, std::memory_order_release);
    if (!jobs.Wait(blockerResult.Value()))
    {
        return 21;
    }
    Result<JobProfile> blockerProfile = jobs.GetProfile(blockerResult.Value());
    if (!blockerProfile || blockerProfile.Value().WorkerIndex != 0 ||
        !jobs.Release(blockerResult.Value()))
    {
        return 22;
    }

    const JobSystemDiagnostics diagnostics = jobs.GetDiagnostics();
    if (diagnostics.StartupEvents != 1 || diagnostics.OperationEvents == 0 ||
        diagnostics.FailureEvents < 2 || diagnostics.SubmittedJobs < 55 ||
        diagnostics.CompletedJobs < 54 || diagnostics.CancelledJobs == 0 ||
        diagnostics.ReleasedJobs < 54 || diagnostics.StolenJobs == 0 ||
        diagnostics.WaitCalls == 0 || diagnostics.ParallelForCalls != 1 ||
        diagnostics.ParallelForBatches == 0 ||
        diagnostics.TotalExecutionNanoseconds == 0 ||
        diagnostics.PersistentBytes == 0 || diagnostics.OutstandingJobs != 0 ||
        diagnostics.WorkerCount != 4 || diagnostics.JobCapacity != 128 ||
        !diagnostics.IsInitialised || !diagnostics.IsAcceptingJobs)
    {
        return 23;
    }

    jobs.Shutdown();
    const JobSystemDiagnostics shutdownDiagnostics = jobs.GetDiagnostics();
    if (shutdownDiagnostics.IsInitialised || shutdownDiagnostics.IsAcceptingJobs ||
        shutdownDiagnostics.ShutdownEvents != 1 || jobs.Submit({Increment, nullptr}))
    {
        return 24;
    }

    return 0;
}

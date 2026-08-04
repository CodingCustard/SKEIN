#include <Skein/Foundation/Synchronization.h>

#include <array>
#include <thread>

int main()
{
    using namespace Skein;

    SynchronizationPrimitivesService invalidService;
    SynchronizationPrimitivesConfig invalidConfig;
    invalidConfig.MaximumLocksPerThread = 0;
    if (invalidService.Initialise(invalidConfig))
    {
        return 1;
    }

    SynchronizationPrimitivesService synchronization;
    SynchronizationPrimitivesConfig config;
    config.MaximumLocksPerThread = 16;
    if (!synchronization.Initialise(config) || synchronization.Initialise(config))
    {
        return 2;
    }

    Mutex lowRankMutex{synchronization, 10};
    Mutex highRankMutex{synchronization, 20};
    if (!highRankMutex.Lock())
    {
        return 3;
    }
    Result<void> orderViolation = lowRankMutex.Lock(Milliseconds{0});
    if (orderViolation || orderViolation.ErrorValue().Code() != ErrorCode::InvalidState ||
        !highRankMutex.Unlock())
    {
        return 4;
    }
    if (!lowRankMutex.Lock() || !highRankMutex.Lock() ||
        !highRankMutex.Unlock() || !lowRankMutex.Unlock())
    {
        return 5;
    }
    if (lowRankMutex.Unlock())
    {
        return 6;
    }

    Mutex contendedMutex{synchronization, 30};
    if (!contendedMutex.Lock())
    {
        return 7;
    }
    bool timedOut = false;
    std::thread timeoutThread{[&]
    {
        Result<void> result = contendedMutex.Lock(Milliseconds{20});
        timedOut = !result && result.ErrorValue().Code() == ErrorCode::Timeout;
    }};
    timeoutThread.join();
    if (!timedOut || !contendedMutex.Unlock())
    {
        return 8;
    }

    i32 sharedValue = 0;
    std::array<std::thread, 4> mutexWorkers;
    std::array<bool, 4> mutexWorkerResults{};
    for (usize workerIndex = 0; workerIndex < mutexWorkers.size(); ++workerIndex)
    {
        mutexWorkers[workerIndex] = std::thread{[&, workerIndex]
        {
            bool succeeded = true;
            for (usize iteration = 0; iteration < 100; ++iteration)
            {
                if (!contendedMutex.Lock())
                {
                    succeeded = false;
                    break;
                }
                ++sharedValue;
                if (!contendedMutex.Unlock())
                {
                    succeeded = false;
                    break;
                }
            }
            mutexWorkerResults[workerIndex] = succeeded;
        }};
    }
    for (std::thread& worker : mutexWorkers)
    {
        worker.join();
    }
    for (const bool succeeded : mutexWorkerResults)
    {
        if (!succeeded)
        {
            return 9;
        }
    }
    if (sharedValue != 400)
    {
        return 10;
    }

    ReadWriteLock readWriteLock{synchronization, 40};
    if (!readWriteLock.LockRead())
    {
        return 11;
    }
    bool parallelReaderSucceeded = false;
    bool blockedWriterTimedOut = false;
    std::thread reader{[&]
    {
        Result<void> result = readWriteLock.LockRead(Milliseconds{100});
        parallelReaderSucceeded = result && readWriteLock.UnlockRead();
    }};
    std::thread writer{[&]
    {
        Result<void> result = readWriteLock.LockWrite(Milliseconds{20});
        blockedWriterTimedOut = !result && result.ErrorValue().Code() == ErrorCode::Timeout;
    }};
    reader.join();
    writer.join();
    if (!parallelReaderSucceeded || !blockedWriterTimedOut || !readWriteLock.UnlockRead() ||
        !readWriteLock.LockWrite() || !readWriteLock.UnlockWrite())
    {
        return 12;
    }

    Event automaticEvent{synchronization, EventResetMode::Auto};
    Result<WaitStatus> automaticTimeout = automaticEvent.Wait(Milliseconds{1});
    if (!automaticTimeout || automaticTimeout.Value() != WaitStatus::TimedOut ||
        !automaticEvent.Signal())
    {
        return 13;
    }
    Result<WaitStatus> automaticSignal = automaticEvent.Wait(Milliseconds{0});
    if (!automaticSignal || automaticSignal.Value() != WaitStatus::Signaled ||
        automaticEvent.IsSignaled())
    {
        return 14;
    }

    Event manualEvent{synchronization, EventResetMode::Manual};
    if (!manualEvent.Signal())
    {
        return 15;
    }
    Result<WaitStatus> firstManualWait = manualEvent.Wait(Milliseconds{0});
    Result<WaitStatus> secondManualWait = manualEvent.Wait(Milliseconds{0});
    if (!firstManualWait || firstManualWait.Value() != WaitStatus::Signaled ||
        !secondManualWait || secondManualWait.Value() != WaitStatus::Signaled ||
        !manualEvent.IsSignaled() || !manualEvent.Reset() || manualEvent.IsSignaled())
    {
        return 16;
    }

    Semaphore invalidSemaphore{synchronization, 2, 1};
    if (invalidSemaphore.IsValid() || invalidSemaphore.Acquire(Milliseconds{0}))
    {
        return 17;
    }
    Semaphore semaphore{synchronization, 0, 2};
    Result<WaitStatus> semaphoreTimeout = semaphore.Acquire(Milliseconds{1});
    if (!semaphoreTimeout || semaphoreTimeout.Value() != WaitStatus::TimedOut ||
        !semaphore.Release(2) || semaphore.Available() != 2 || semaphore.Release())
    {
        return 18;
    }
    Result<WaitStatus> firstPermit = semaphore.Acquire(Milliseconds{0});
    Result<WaitStatus> secondPermit = semaphore.Acquire(Milliseconds{0});
    Result<WaitStatus> missingPermit = semaphore.Acquire(Milliseconds{0});
    if (!firstPermit || firstPermit.Value() != WaitStatus::Signaled ||
        !secondPermit || secondPermit.Value() != WaitStatus::Signaled ||
        !missingPermit || missingPermit.Value() != WaitStatus::TimedOut ||
        semaphore.Available() != 0)
    {
        return 19;
    }

    Atomic<u32> atomicCounter{0};
    std::array<std::thread, 4> atomicWorkers;
    for (std::thread& worker : atomicWorkers)
    {
        worker = std::thread{[&]
        {
            for (usize iteration = 0; iteration < 1000; ++iteration)
            {
                (void)atomicCounter.FetchAdd(1, MemoryOrder::Relaxed);
            }
        }};
    }
    for (std::thread& worker : atomicWorkers)
    {
        worker.join();
    }
    u32 expected = 4000;
    if (atomicCounter.Load(MemoryOrder::Acquire) != expected ||
        !atomicCounter.CompareExchangeStrong(expected, 17, MemoryOrder::AcquireRelease, MemoryOrder::Acquire))
    {
        return 20;
    }

    Atomic<u32> normalizedOrders{0};
    normalizedOrders.Store(1, MemoryOrder::Acquire);
    u32 mismatchedExpected = 0;
    if (normalizedOrders.Load(MemoryOrder::Release) != 1 ||
        normalizedOrders.CompareExchangeStrong(
            mismatchedExpected,
            2,
            MemoryOrder::Release,
            MemoryOrder::AcquireRelease) ||
        mismatchedExpected != 1)
    {
        return 21;
    }

    const ContentionMetrics mutexMetrics = contendedMutex.GetMetrics();
    const ContentionMetrics readWriteMetrics = readWriteLock.GetMetrics();
    const SynchronizationDiagnostics diagnostics = synchronization.GetDiagnostics();
    if (mutexMetrics.Acquisitions < 401 || mutexMetrics.Contentions == 0 ||
        mutexMetrics.Timeouts == 0 || mutexMetrics.TotalWaitNanoseconds == 0 ||
        mutexMetrics.MaximumWaitNanoseconds == 0 || readWriteMetrics.Acquisitions < 3 ||
        diagnostics.StartupEvents != 1 || diagnostics.OperationEvents == 0 ||
        diagnostics.FailureEvents < 5 || diagnostics.Acquisitions == 0 ||
        diagnostics.Contentions == 0 || diagnostics.Timeouts < 3 ||
        diagnostics.OrderViolations == 0 || diagnostics.EventSignals != 2 ||
        diagnostics.SemaphoreReleases != 1 || diagnostics.TotalWaitNanoseconds == 0 ||
        diagnostics.MaximumWaitNanoseconds == 0 || !diagnostics.IsInitialised)
    {
        return 22;
    }

    synchronization.Shutdown();
    const SynchronizationDiagnostics shutdownDiagnostics = synchronization.GetDiagnostics();
    if (shutdownDiagnostics.IsInitialised || shutdownDiagnostics.ShutdownEvents != 1 ||
        contendedMutex.Lock(Milliseconds{0}))
    {
        return 23;
    }

    return 0;
}

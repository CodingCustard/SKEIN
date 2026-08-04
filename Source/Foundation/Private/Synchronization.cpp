#include <Skein/Foundation/Synchronization.h>

#include <Skein/Foundation/Assert.h>
#include <Skein/Foundation/Build.h>
#include <Skein/Foundation/Log.h>

#include <array>
#include <chrono>
#include <limits>

namespace Skein
{
    namespace
    {
        enum class HeldLockMode : u8
        {
            Exclusive,
            Shared
        };

        struct HeldLock final
        {
            const void* Address = nullptr;
            LockRank Rank = 0;
            HeldLockMode Mode = HeldLockMode::Exclusive;
        };

        struct HeldLockStack final
        {
            std::array<
                HeldLock,
                SynchronizationPrimitivesConfig::MaximumSupportedLocksPerThread> Locks{};
            usize Size = 0;
        };

        thread_local HeldLockStack ThreadLocks;
        thread_local const u8 ThreadIdentity = 0;

        void TraceSynchronization(const StringView message)
        {
#if SKEIN_ENABLE_TRACING
            Log(LogLevel::Trace, "Synchronization", message);
#else
            (void)message;
#endif
        }

        [[nodiscard]] u64 CurrentThreadToken() noexcept
        {
            return static_cast<u64>(reinterpret_cast<std::uintptr_t>(&ThreadIdentity));
        }

        [[nodiscard]] bool CanAcquire(
            const void* const address,
            const LockRank rank,
            const usize maximumLocks) noexcept
        {
            if (ThreadLocks.Size >= maximumLocks || ThreadLocks.Size >= ThreadLocks.Locks.size())
            {
                return false;
            }
            for (usize index = 0; index < ThreadLocks.Size; ++index)
            {
                const HeldLock& held = ThreadLocks.Locks[index];
                if (held.Address == address)
                {
                    return false;
                }
                if (rank != 0 && held.Rank != 0 && rank <= held.Rank)
                {
                    return false;
                }
            }
            return true;
        }

        void RecordThreadAcquire(
            const void* const address,
            const LockRank rank,
            const HeldLockMode mode) noexcept
        {
            ThreadLocks.Locks[ThreadLocks.Size++] = HeldLock{address, rank, mode};
        }

        [[nodiscard]] bool CanRelease(
            const void* const address,
            const HeldLockMode mode) noexcept
        {
            if (ThreadLocks.Size == 0)
            {
                return false;
            }
            const HeldLock& held = ThreadLocks.Locks[ThreadLocks.Size - 1];
            return held.Address == address && held.Mode == mode;
        }

        void RecordThreadRelease() noexcept
        {
            --ThreadLocks.Size;
            ThreadLocks.Locks[ThreadLocks.Size] = {};
        }

        [[nodiscard]] u64 ElapsedNanoseconds(
            const std::chrono::steady_clock::time_point start) noexcept
        {
            const auto elapsed = std::chrono::duration_cast<Nanoseconds>(
                std::chrono::steady_clock::now() - start).count();
            return elapsed <= 0 ? 0 : static_cast<u64>(elapsed);
        }

        void UpdateMaximum(std::atomic<u64>& maximum, const u64 candidate) noexcept
        {
            u64 current = maximum.load(std::memory_order_relaxed);
            while (candidate > current && !maximum.compare_exchange_weak(
                current,
                candidate,
                std::memory_order_relaxed,
                std::memory_order_relaxed))
            {
            }
        }

        [[nodiscard]] bool IsInfiniteTimeout(const Milliseconds timeout) noexcept
        {
            return timeout == Milliseconds::max();
        }

        [[nodiscard]] bool IsValidTimeout(const Milliseconds timeout) noexcept
        {
            return IsInfiniteTimeout(timeout) || timeout.count() >= 0;
        }
    }

    SynchronizationPrimitivesService::~SynchronizationPrimitivesService()
    {
        Shutdown();
    }

    Result<void> SynchronizationPrimitivesService::Initialise(
        const SynchronizationPrimitivesConfig& config) noexcept
    {
        if (config.MaximumLocksPerThread == 0 ||
            config.MaximumLocksPerThread >
                SynchronizationPrimitivesConfig::MaximumSupportedLocksPerThread)
        {
            m_failureEvents.fetch_add(1, std::memory_order_relaxed);
            return Unexpected{Error{
                ErrorCode::InvalidArgument,
                "invalid maximum locks per thread"}};
        }

        bool expected = false;
        if (!m_initialised.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire))
        {
            m_failureEvents.fetch_add(1, std::memory_order_relaxed);
            return Unexpected{Error{
                ErrorCode::InvalidState,
                "synchronization service is already initialised"}};
        }

        m_maximumLocksPerThread.store(config.MaximumLocksPerThread, std::memory_order_relaxed);
        m_metricsEnabled.store(config.EnableContentionMetrics, std::memory_order_relaxed);
        m_startupEvents.store(1, std::memory_order_relaxed);
        m_operationEvents.store(0, std::memory_order_relaxed);
        m_failureEvents.store(0, std::memory_order_relaxed);
        m_shutdownEvents.store(0, std::memory_order_relaxed);
        m_acquisitions.store(0, std::memory_order_relaxed);
        m_contentions.store(0, std::memory_order_relaxed);
        m_timeouts.store(0, std::memory_order_relaxed);
        m_orderViolations.store(0, std::memory_order_relaxed);
        m_eventSignals.store(0, std::memory_order_relaxed);
        m_semaphoreReleases.store(0, std::memory_order_relaxed);
        m_totalWaitNanoseconds.store(0, std::memory_order_relaxed);
        m_maximumWaitNanoseconds.store(0, std::memory_order_relaxed);
        TraceSynchronization("synchronization service initialised");
        return {};
    }

    void SynchronizationPrimitivesService::Shutdown() noexcept
    {
        if (!m_initialised.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }
        m_shutdownEvents.fetch_add(1, std::memory_order_relaxed);
        TraceSynchronization("synchronization service shutdown");
    }

    bool SynchronizationPrimitivesService::IsInitialised() const noexcept
    {
        return m_initialised.load(std::memory_order_acquire);
    }

    SynchronizationDiagnostics SynchronizationPrimitivesService::GetDiagnostics() const noexcept
    {
        return SynchronizationDiagnostics{
            m_startupEvents.load(std::memory_order_relaxed),
            m_operationEvents.load(std::memory_order_relaxed),
            m_failureEvents.load(std::memory_order_relaxed),
            m_shutdownEvents.load(std::memory_order_relaxed),
            m_acquisitions.load(std::memory_order_relaxed),
            m_contentions.load(std::memory_order_relaxed),
            m_timeouts.load(std::memory_order_relaxed),
            m_orderViolations.load(std::memory_order_relaxed),
            m_eventSignals.load(std::memory_order_relaxed),
            m_semaphoreReleases.load(std::memory_order_relaxed),
            m_totalWaitNanoseconds.load(std::memory_order_relaxed),
            m_maximumWaitNanoseconds.load(std::memory_order_relaxed),
            IsInitialised()};
    }

    usize SynchronizationPrimitivesService::MaximumLocksPerThread() const noexcept
    {
        return m_maximumLocksPerThread.load(std::memory_order_relaxed);
    }

    bool SynchronizationPrimitivesService::MetricsEnabled() const noexcept
    {
        return m_metricsEnabled.load(std::memory_order_relaxed);
    }

    void SynchronizationPrimitivesService::RecordOperation() noexcept
    {
        m_operationEvents.fetch_add(1, std::memory_order_relaxed);
        TraceSynchronization("synchronization operation completed");
    }

    void SynchronizationPrimitivesService::RecordAcquisition(
        const u64 waitNanoseconds,
        const bool contended) noexcept
    {
        m_acquisitions.fetch_add(1, std::memory_order_relaxed);
        if (contended)
        {
            m_contentions.fetch_add(1, std::memory_order_relaxed);
        }
        if (MetricsEnabled())
        {
            m_totalWaitNanoseconds.fetch_add(waitNanoseconds, std::memory_order_relaxed);
            UpdateMaximum(m_maximumWaitNanoseconds, waitNanoseconds);
        }
        RecordOperation();
    }

    void SynchronizationPrimitivesService::RecordFailure(
        const bool timeout,
        const bool orderViolation,
        const u64 waitNanoseconds) noexcept
    {
        m_failureEvents.fetch_add(1, std::memory_order_relaxed);
        if (timeout)
        {
            m_timeouts.fetch_add(1, std::memory_order_relaxed);
            m_contentions.fetch_add(1, std::memory_order_relaxed);
            if (MetricsEnabled())
            {
                m_totalWaitNanoseconds.fetch_add(waitNanoseconds, std::memory_order_relaxed);
                UpdateMaximum(m_maximumWaitNanoseconds, waitNanoseconds);
            }
        }
        if (orderViolation)
        {
            m_orderViolations.fetch_add(1, std::memory_order_relaxed);
        }
        TraceSynchronization("synchronization operation failed");
    }

    void SynchronizationPrimitivesService::RecordEventSignal() noexcept
    {
        m_eventSignals.fetch_add(1, std::memory_order_relaxed);
        RecordOperation();
    }

    void SynchronizationPrimitivesService::RecordSemaphoreRelease() noexcept
    {
        m_semaphoreReleases.fetch_add(1, std::memory_order_relaxed);
        RecordOperation();
    }

    Mutex::Mutex(
        SynchronizationPrimitivesService& service,
        const LockRank rank) noexcept
        : m_service(&service),
          m_rank(rank)
    {
    }

    Mutex::~Mutex()
    {
        (void)SKEIN_ASSERT_MESSAGE(
            m_ownerThread.load(std::memory_order_relaxed) == 0,
            "destroying a locked mutex");
    }

    Result<void> Mutex::Lock(const Milliseconds timeout) noexcept
    {
        if (!m_service->IsInitialised())
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "synchronization service is not initialised"}};
        }
        if (!IsValidTimeout(timeout))
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid mutex timeout"}};
        }

        m_attempts.fetch_add(1, std::memory_order_relaxed);
        if (!CanAcquire(this, m_rank, m_service->MaximumLocksPerThread()))
        {
            m_orderViolations.fetch_add(1, std::memory_order_relaxed);
            m_service->RecordFailure(false, true);
            return Unexpected{Error{ErrorCode::InvalidState, "mutex lock order violation"}};
        }

        const auto start = std::chrono::steady_clock::now();
        bool contended = false;
        bool acquired = m_mutex.try_lock();
        if (!acquired)
        {
            contended = true;
            m_contentions.fetch_add(1, std::memory_order_relaxed);
            if (IsInfiniteTimeout(timeout))
            {
                m_mutex.lock();
                acquired = true;
            }
            else
            {
                acquired = m_mutex.try_lock_for(timeout);
            }
        }

        const u64 waitNanoseconds = ElapsedNanoseconds(start);
        if (!acquired)
        {
            m_timeouts.fetch_add(1, std::memory_order_relaxed);
            if (m_service->MetricsEnabled())
            {
                m_totalWaitNanoseconds.fetch_add(waitNanoseconds, std::memory_order_relaxed);
                UpdateMaximum(m_maximumWaitNanoseconds, waitNanoseconds);
            }
            m_service->RecordFailure(true, false, waitNanoseconds);
            return Unexpected{Error{ErrorCode::Timeout, "mutex lock timed out"}};
        }

        RecordThreadAcquire(this, m_rank, HeldLockMode::Exclusive);
        m_ownerThread.store(CurrentThreadToken(), std::memory_order_release);
        m_acquisitions.fetch_add(1, std::memory_order_relaxed);
        if (m_service->MetricsEnabled())
        {
            m_totalWaitNanoseconds.fetch_add(waitNanoseconds, std::memory_order_relaxed);
            UpdateMaximum(m_maximumWaitNanoseconds, waitNanoseconds);
        }
        m_service->RecordAcquisition(waitNanoseconds, contended);
        return {};
    }

    Result<bool> Mutex::TryLock() noexcept
    {
        if (!m_service->IsInitialised())
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "synchronization service is not initialised"}};
        }
        m_attempts.fetch_add(1, std::memory_order_relaxed);
        if (!CanAcquire(this, m_rank, m_service->MaximumLocksPerThread()))
        {
            m_orderViolations.fetch_add(1, std::memory_order_relaxed);
            m_service->RecordFailure(false, true);
            return Unexpected{Error{ErrorCode::InvalidState, "mutex lock order violation"}};
        }
        if (!m_mutex.try_lock())
        {
            m_contentions.fetch_add(1, std::memory_order_relaxed);
            m_service->RecordOperation();
            return false;
        }
        RecordThreadAcquire(this, m_rank, HeldLockMode::Exclusive);
        m_ownerThread.store(CurrentThreadToken(), std::memory_order_release);
        m_acquisitions.fetch_add(1, std::memory_order_relaxed);
        m_service->RecordAcquisition(0, false);
        return true;
    }

    Result<void> Mutex::Unlock() noexcept
    {
        if (m_ownerThread.load(std::memory_order_acquire) != CurrentThreadToken() ||
            !CanRelease(this, HeldLockMode::Exclusive))
        {
            m_ownershipViolations.fetch_add(1, std::memory_order_relaxed);
            if (m_service->IsInitialised())
            {
                m_service->RecordFailure();
            }
            return Unexpected{Error{ErrorCode::InvalidState, "mutex unlock ownership violation"}};
        }
        m_ownerThread.store(0, std::memory_order_release);
        RecordThreadRelease();
        m_mutex.unlock();
        if (m_service->IsInitialised())
        {
            m_service->RecordOperation();
        }
        return {};
    }

    ContentionMetrics Mutex::GetMetrics() const noexcept
    {
        return ContentionMetrics{
            m_attempts.load(std::memory_order_relaxed),
            m_acquisitions.load(std::memory_order_relaxed),
            m_contentions.load(std::memory_order_relaxed),
            m_timeouts.load(std::memory_order_relaxed),
            m_orderViolations.load(std::memory_order_relaxed),
            m_ownershipViolations.load(std::memory_order_relaxed),
            m_totalWaitNanoseconds.load(std::memory_order_relaxed),
            m_maximumWaitNanoseconds.load(std::memory_order_relaxed)};
    }

    ReadWriteLock::ReadWriteLock(
        SynchronizationPrimitivesService& service,
        const LockRank rank) noexcept
        : m_service(&service),
          m_rank(rank)
    {
    }

    ReadWriteLock::~ReadWriteLock()
    {
        (void)SKEIN_ASSERT_MESSAGE(
            m_writerThread.load(std::memory_order_relaxed) == 0 &&
                m_activeReaders.load(std::memory_order_relaxed) == 0,
            "destroying a locked reader/writer lock");
    }

    Result<void> ReadWriteLock::LockRead(const Milliseconds timeout) noexcept
    {
        return Lock(false, timeout);
    }

    Result<bool> ReadWriteLock::TryLockRead() noexcept
    {
        return TryLock(false);
    }

    Result<void> ReadWriteLock::UnlockRead() noexcept
    {
        return Unlock(false);
    }

    Result<void> ReadWriteLock::LockWrite(const Milliseconds timeout) noexcept
    {
        return Lock(true, timeout);
    }

    Result<bool> ReadWriteLock::TryLockWrite() noexcept
    {
        return TryLock(true);
    }

    Result<void> ReadWriteLock::UnlockWrite() noexcept
    {
        return Unlock(true);
    }

    Result<void> ReadWriteLock::Lock(
        const bool write,
        const Milliseconds timeout) noexcept
    {
        if (!m_service->IsInitialised())
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "synchronization service is not initialised"}};
        }
        if (!IsValidTimeout(timeout))
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid reader/writer lock timeout"}};
        }
        m_attempts.fetch_add(1, std::memory_order_relaxed);
        if (!CanAcquire(this, m_rank, m_service->MaximumLocksPerThread()))
        {
            m_orderViolations.fetch_add(1, std::memory_order_relaxed);
            m_service->RecordFailure(false, true);
            return Unexpected{Error{ErrorCode::InvalidState, "reader/writer lock order violation"}};
        }

        const auto start = std::chrono::steady_clock::now();
        bool contended = false;
        bool acquired = write ? m_mutex.try_lock() : m_mutex.try_lock_shared();
        if (!acquired)
        {
            contended = true;
            m_contentions.fetch_add(1, std::memory_order_relaxed);
            if (IsInfiniteTimeout(timeout))
            {
                if (write)
                {
                    m_mutex.lock();
                }
                else
                {
                    m_mutex.lock_shared();
                }
                acquired = true;
            }
            else
            {
                acquired = write
                    ? m_mutex.try_lock_for(timeout)
                    : m_mutex.try_lock_shared_for(timeout);
            }
        }
        const u64 waitNanoseconds = ElapsedNanoseconds(start);
        if (!acquired)
        {
            m_timeouts.fetch_add(1, std::memory_order_relaxed);
            if (m_service->MetricsEnabled())
            {
                m_totalWaitNanoseconds.fetch_add(waitNanoseconds, std::memory_order_relaxed);
                UpdateMaximum(m_maximumWaitNanoseconds, waitNanoseconds);
            }
            m_service->RecordFailure(true, false, waitNanoseconds);
            return Unexpected{Error{ErrorCode::Timeout, "reader/writer lock timed out"}};
        }

        RecordThreadAcquire(
            this,
            m_rank,
            write ? HeldLockMode::Exclusive : HeldLockMode::Shared);
        if (write)
        {
            m_writerThread.store(CurrentThreadToken(), std::memory_order_release);
        }
        else
        {
            m_activeReaders.fetch_add(1, std::memory_order_relaxed);
        }
        m_acquisitions.fetch_add(1, std::memory_order_relaxed);
        if (m_service->MetricsEnabled())
        {
            m_totalWaitNanoseconds.fetch_add(waitNanoseconds, std::memory_order_relaxed);
            UpdateMaximum(m_maximumWaitNanoseconds, waitNanoseconds);
        }
        m_service->RecordAcquisition(waitNanoseconds, contended);
        return {};
    }

    Result<bool> ReadWriteLock::TryLock(const bool write) noexcept
    {
        if (!m_service->IsInitialised())
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "synchronization service is not initialised"}};
        }
        m_attempts.fetch_add(1, std::memory_order_relaxed);
        if (!CanAcquire(this, m_rank, m_service->MaximumLocksPerThread()))
        {
            m_orderViolations.fetch_add(1, std::memory_order_relaxed);
            m_service->RecordFailure(false, true);
            return Unexpected{Error{ErrorCode::InvalidState, "reader/writer lock order violation"}};
        }
        const bool acquired = write ? m_mutex.try_lock() : m_mutex.try_lock_shared();
        if (!acquired)
        {
            m_contentions.fetch_add(1, std::memory_order_relaxed);
            m_service->RecordOperation();
            return false;
        }
        RecordThreadAcquire(
            this,
            m_rank,
            write ? HeldLockMode::Exclusive : HeldLockMode::Shared);
        if (write)
        {
            m_writerThread.store(CurrentThreadToken(), std::memory_order_release);
        }
        else
        {
            m_activeReaders.fetch_add(1, std::memory_order_relaxed);
        }
        m_acquisitions.fetch_add(1, std::memory_order_relaxed);
        m_service->RecordAcquisition(0, false);
        return true;
    }

    Result<void> ReadWriteLock::Unlock(const bool write) noexcept
    {
        const HeldLockMode mode = write ? HeldLockMode::Exclusive : HeldLockMode::Shared;
        const bool validOwner = write
            ? m_writerThread.load(std::memory_order_acquire) == CurrentThreadToken()
            : m_activeReaders.load(std::memory_order_relaxed) != 0;
        if (!validOwner || !CanRelease(this, mode))
        {
            m_ownershipViolations.fetch_add(1, std::memory_order_relaxed);
            if (m_service->IsInitialised())
            {
                m_service->RecordFailure();
            }
            return Unexpected{Error{ErrorCode::InvalidState, "reader/writer unlock ownership violation"}};
        }

        RecordThreadRelease();
        if (write)
        {
            m_writerThread.store(0, std::memory_order_release);
            m_mutex.unlock();
        }
        else
        {
            m_activeReaders.fetch_sub(1, std::memory_order_relaxed);
            m_mutex.unlock_shared();
        }
        if (m_service->IsInitialised())
        {
            m_service->RecordOperation();
        }
        return {};
    }

    ContentionMetrics ReadWriteLock::GetMetrics() const noexcept
    {
        return ContentionMetrics{
            m_attempts.load(std::memory_order_relaxed),
            m_acquisitions.load(std::memory_order_relaxed),
            m_contentions.load(std::memory_order_relaxed),
            m_timeouts.load(std::memory_order_relaxed),
            m_orderViolations.load(std::memory_order_relaxed),
            m_ownershipViolations.load(std::memory_order_relaxed),
            m_totalWaitNanoseconds.load(std::memory_order_relaxed),
            m_maximumWaitNanoseconds.load(std::memory_order_relaxed)};
    }

    Event::Event(
        SynchronizationPrimitivesService& service,
        const EventResetMode resetMode,
        const bool initiallySignaled) noexcept
        : m_service(&service),
          m_resetMode(resetMode),
          m_signaled(initiallySignaled)
    {
    }

    Result<void> Event::Signal() noexcept
    {
        if (!m_service->IsInitialised())
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "synchronization service is not initialised"}};
        }
        {
            std::scoped_lock lock{m_mutex};
            m_signaled = true;
        }
        if (m_resetMode == EventResetMode::Manual)
        {
            m_condition.notify_all();
        }
        else
        {
            m_condition.notify_one();
        }
        m_service->RecordEventSignal();
        return {};
    }

    Result<void> Event::Reset() noexcept
    {
        if (!m_service->IsInitialised())
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "synchronization service is not initialised"}};
        }
        std::scoped_lock lock{m_mutex};
        m_signaled = false;
        m_service->RecordOperation();
        return {};
    }

    Result<WaitStatus> Event::Wait(const Milliseconds timeout) noexcept
    {
        if (!m_service->IsInitialised())
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "synchronization service is not initialised"}};
        }
        if (!IsValidTimeout(timeout))
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid event timeout"}};
        }

        m_attempts.fetch_add(1, std::memory_order_relaxed);
        const auto start = std::chrono::steady_clock::now();
        std::unique_lock lock{m_mutex};
        const bool contended = !m_signaled;
        if (contended)
        {
            m_contentions.fetch_add(1, std::memory_order_relaxed);
        }
        bool signaled = true;
        if (IsInfiniteTimeout(timeout))
        {
            m_condition.wait(lock, [this] { return m_signaled; });
        }
        else
        {
            signaled = m_condition.wait_for(lock, timeout, [this] { return m_signaled; });
        }
        const u64 waitNanoseconds = ElapsedNanoseconds(start);
        if (!signaled)
        {
            m_timeouts.fetch_add(1, std::memory_order_relaxed);
            if (m_service->MetricsEnabled())
            {
                m_totalWaitNanoseconds.fetch_add(waitNanoseconds, std::memory_order_relaxed);
                UpdateMaximum(m_maximumWaitNanoseconds, waitNanoseconds);
            }
            m_service->RecordFailure(true, false, waitNanoseconds);
            return WaitStatus::TimedOut;
        }
        if (m_resetMode == EventResetMode::Auto)
        {
            m_signaled = false;
        }
        m_acquisitions.fetch_add(1, std::memory_order_relaxed);
        if (m_service->MetricsEnabled())
        {
            m_totalWaitNanoseconds.fetch_add(waitNanoseconds, std::memory_order_relaxed);
            UpdateMaximum(m_maximumWaitNanoseconds, waitNanoseconds);
        }
        m_service->RecordAcquisition(waitNanoseconds, contended);
        return WaitStatus::Signaled;
    }

    bool Event::IsSignaled() const noexcept
    {
        std::scoped_lock lock{m_mutex};
        return m_signaled;
    }

    ContentionMetrics Event::GetMetrics() const noexcept
    {
        return ContentionMetrics{
            m_attempts.load(std::memory_order_relaxed),
            m_acquisitions.load(std::memory_order_relaxed),
            m_contentions.load(std::memory_order_relaxed),
            m_timeouts.load(std::memory_order_relaxed),
            0,
            0,
            m_totalWaitNanoseconds.load(std::memory_order_relaxed),
            m_maximumWaitNanoseconds.load(std::memory_order_relaxed)};
    }

    Semaphore::Semaphore(
        SynchronizationPrimitivesService& service,
        const usize initialCount,
        const usize maximumCount) noexcept
        : m_service(&service),
          m_count(initialCount),
          m_maximumCount(maximumCount),
          m_valid(maximumCount != 0 && initialCount <= maximumCount)
    {
    }

    bool Semaphore::IsValid() const noexcept
    {
        return m_valid;
    }

    Result<void> Semaphore::Release(const usize count) noexcept
    {
        if (!m_service->IsInitialised())
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "synchronization service is not initialised"}};
        }
        if (count == 0 || m_maximumCount == 0)
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid semaphore release"}};
        }
        {
            std::scoped_lock lock{m_mutex};
            if (m_count > m_maximumCount || count > m_maximumCount - m_count)
            {
                m_service->RecordFailure();
                return Unexpected{Error{ErrorCode::InvalidState, "semaphore count would exceed maximum"}};
            }
            m_count += count;
        }
        m_condition.notify_all();
        m_service->RecordSemaphoreRelease();
        return {};
    }

    Result<WaitStatus> Semaphore::Acquire(const Milliseconds timeout) noexcept
    {
        if (!m_service->IsInitialised())
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidState, "synchronization service is not initialised"}};
        }
        if (!IsValid() || !IsValidTimeout(timeout))
        {
            m_service->RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid semaphore or timeout"}};
        }

        m_attempts.fetch_add(1, std::memory_order_relaxed);
        const auto start = std::chrono::steady_clock::now();
        std::unique_lock lock{m_mutex};
        const bool contended = m_count == 0;
        if (contended)
        {
            m_contentions.fetch_add(1, std::memory_order_relaxed);
        }
        bool acquired = true;
        if (IsInfiniteTimeout(timeout))
        {
            m_condition.wait(lock, [this] { return m_count != 0; });
        }
        else
        {
            acquired = m_condition.wait_for(lock, timeout, [this] { return m_count != 0; });
        }
        const u64 waitNanoseconds = ElapsedNanoseconds(start);
        if (!acquired)
        {
            m_timeouts.fetch_add(1, std::memory_order_relaxed);
            if (m_service->MetricsEnabled())
            {
                m_totalWaitNanoseconds.fetch_add(waitNanoseconds, std::memory_order_relaxed);
                UpdateMaximum(m_maximumWaitNanoseconds, waitNanoseconds);
            }
            m_service->RecordFailure(true, false, waitNanoseconds);
            return WaitStatus::TimedOut;
        }
        --m_count;
        m_acquisitions.fetch_add(1, std::memory_order_relaxed);
        if (m_service->MetricsEnabled())
        {
            m_totalWaitNanoseconds.fetch_add(waitNanoseconds, std::memory_order_relaxed);
            UpdateMaximum(m_maximumWaitNanoseconds, waitNanoseconds);
        }
        m_service->RecordAcquisition(waitNanoseconds, contended);
        return WaitStatus::Signaled;
    }

    usize Semaphore::Available() const noexcept
    {
        std::scoped_lock lock{m_mutex};
        return m_count;
    }

    ContentionMetrics Semaphore::GetMetrics() const noexcept
    {
        return ContentionMetrics{
            m_attempts.load(std::memory_order_relaxed),
            m_acquisitions.load(std::memory_order_relaxed),
            m_contentions.load(std::memory_order_relaxed),
            m_timeouts.load(std::memory_order_relaxed),
            0,
            0,
            m_totalWaitNanoseconds.load(std::memory_order_relaxed),
            m_maximumWaitNanoseconds.load(std::memory_order_relaxed)};
    }
}

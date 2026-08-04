#pragma once

#include <Skein/Foundation/Result.h>
#include <Skein/Foundation/Types.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <type_traits>

namespace Skein
{
    using LockRank = u32;

    enum class WaitStatus : u8
    {
        Signaled,
        TimedOut
    };

    enum class EventResetMode : u8
    {
        Auto,
        Manual
    };

    enum class MemoryOrder : u8
    {
        Relaxed,
        Acquire,
        Release,
        AcquireRelease,
        SequentiallyConsistent
    };

    struct ContentionMetrics final
    {
        u64 Attempts = 0;
        u64 Acquisitions = 0;
        u64 Contentions = 0;
        u64 Timeouts = 0;
        u64 OrderViolations = 0;
        u64 OwnershipViolations = 0;
        u64 TotalWaitNanoseconds = 0;
        u64 MaximumWaitNanoseconds = 0;
    };

    struct SynchronizationPrimitivesConfig final
    {
        static constexpr usize MaximumSupportedLocksPerThread = 64;

        usize MaximumLocksPerThread = 32;
        bool EnableContentionMetrics = true;
    };

    struct SynchronizationDiagnostics final
    {
        u64 StartupEvents = 0;
        u64 OperationEvents = 0;
        u64 FailureEvents = 0;
        u64 ShutdownEvents = 0;
        u64 Acquisitions = 0;
        u64 Contentions = 0;
        u64 Timeouts = 0;
        u64 OrderViolations = 0;
        u64 EventSignals = 0;
        u64 SemaphoreReleases = 0;
        u64 TotalWaitNanoseconds = 0;
        u64 MaximumWaitNanoseconds = 0;
        bool IsInitialised = false;
    };

    class Mutex;
    class ReadWriteLock;
    class Event;
    class Semaphore;

    class SynchronizationPrimitivesService final
    {
    public:
        SynchronizationPrimitivesService() noexcept = default;
        SynchronizationPrimitivesService(const SynchronizationPrimitivesService&) = delete;
        SynchronizationPrimitivesService& operator=(const SynchronizationPrimitivesService&) = delete;
        ~SynchronizationPrimitivesService();

        [[nodiscard]] Result<void> Initialise(
            const SynchronizationPrimitivesConfig& config = {}) noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialised() const noexcept;
        [[nodiscard]] SynchronizationDiagnostics GetDiagnostics() const noexcept;

    private:
        friend class Mutex;
        friend class ReadWriteLock;
        friend class Event;
        friend class Semaphore;

        [[nodiscard]] usize MaximumLocksPerThread() const noexcept;
        [[nodiscard]] bool MetricsEnabled() const noexcept;
        void RecordOperation() noexcept;
        void RecordAcquisition(u64 waitNanoseconds, bool contended) noexcept;
        void RecordFailure(
            bool timeout = false,
            bool orderViolation = false,
            u64 waitNanoseconds = 0) noexcept;
        void RecordEventSignal() noexcept;
        void RecordSemaphoreRelease() noexcept;

        std::atomic<usize> m_maximumLocksPerThread = 0;
        std::atomic<bool> m_metricsEnabled = false;
        std::atomic<bool> m_initialised = false;
        std::atomic<u64> m_startupEvents = 0;
        std::atomic<u64> m_operationEvents = 0;
        std::atomic<u64> m_failureEvents = 0;
        std::atomic<u64> m_shutdownEvents = 0;
        std::atomic<u64> m_acquisitions = 0;
        std::atomic<u64> m_contentions = 0;
        std::atomic<u64> m_timeouts = 0;
        std::atomic<u64> m_orderViolations = 0;
        std::atomic<u64> m_eventSignals = 0;
        std::atomic<u64> m_semaphoreReleases = 0;
        std::atomic<u64> m_totalWaitNanoseconds = 0;
        std::atomic<u64> m_maximumWaitNanoseconds = 0;
    };

    class Mutex final
    {
    public:
        explicit Mutex(
            SynchronizationPrimitivesService& service,
            LockRank rank = 0) noexcept;
        Mutex(const Mutex&) = delete;
        Mutex& operator=(const Mutex&) = delete;
        ~Mutex();

        [[nodiscard]] Result<void> Lock(
            Milliseconds timeout = Milliseconds::max()) noexcept;
        [[nodiscard]] Result<bool> TryLock() noexcept;
        [[nodiscard]] Result<void> Unlock() noexcept;

        [[nodiscard]] LockRank Rank() const noexcept { return m_rank; }
        [[nodiscard]] ContentionMetrics GetMetrics() const noexcept;

    private:
        SynchronizationPrimitivesService* m_service = nullptr;
        std::timed_mutex m_mutex;
        LockRank m_rank = 0;
        std::atomic<u64> m_ownerThread = 0;
        std::atomic<u64> m_attempts = 0;
        std::atomic<u64> m_acquisitions = 0;
        std::atomic<u64> m_contentions = 0;
        std::atomic<u64> m_timeouts = 0;
        std::atomic<u64> m_orderViolations = 0;
        std::atomic<u64> m_ownershipViolations = 0;
        std::atomic<u64> m_totalWaitNanoseconds = 0;
        std::atomic<u64> m_maximumWaitNanoseconds = 0;
    };

    class ReadWriteLock final
    {
    public:
        explicit ReadWriteLock(
            SynchronizationPrimitivesService& service,
            LockRank rank = 0) noexcept;
        ReadWriteLock(const ReadWriteLock&) = delete;
        ReadWriteLock& operator=(const ReadWriteLock&) = delete;
        ~ReadWriteLock();

        [[nodiscard]] Result<void> LockRead(
            Milliseconds timeout = Milliseconds::max()) noexcept;
        [[nodiscard]] Result<bool> TryLockRead() noexcept;
        [[nodiscard]] Result<void> UnlockRead() noexcept;
        [[nodiscard]] Result<void> LockWrite(
            Milliseconds timeout = Milliseconds::max()) noexcept;
        [[nodiscard]] Result<bool> TryLockWrite() noexcept;
        [[nodiscard]] Result<void> UnlockWrite() noexcept;

        [[nodiscard]] LockRank Rank() const noexcept { return m_rank; }
        [[nodiscard]] ContentionMetrics GetMetrics() const noexcept;

    private:
        [[nodiscard]] Result<void> Lock(bool write, Milliseconds timeout) noexcept;
        [[nodiscard]] Result<bool> TryLock(bool write) noexcept;
        [[nodiscard]] Result<void> Unlock(bool write) noexcept;

        SynchronizationPrimitivesService* m_service = nullptr;
        std::shared_timed_mutex m_mutex;
        LockRank m_rank = 0;
        std::atomic<u64> m_writerThread = 0;
        std::atomic<u64> m_activeReaders = 0;
        std::atomic<u64> m_attempts = 0;
        std::atomic<u64> m_acquisitions = 0;
        std::atomic<u64> m_contentions = 0;
        std::atomic<u64> m_timeouts = 0;
        std::atomic<u64> m_orderViolations = 0;
        std::atomic<u64> m_ownershipViolations = 0;
        std::atomic<u64> m_totalWaitNanoseconds = 0;
        std::atomic<u64> m_maximumWaitNanoseconds = 0;
    };

    class Event final
    {
    public:
        explicit Event(
            SynchronizationPrimitivesService& service,
            EventResetMode resetMode = EventResetMode::Auto,
            bool initiallySignaled = false) noexcept;
        Event(const Event&) = delete;
        Event& operator=(const Event&) = delete;

        [[nodiscard]] Result<void> Signal() noexcept;
        [[nodiscard]] Result<void> Reset() noexcept;
        [[nodiscard]] Result<WaitStatus> Wait(
            Milliseconds timeout = Milliseconds::max()) noexcept;
        [[nodiscard]] bool IsSignaled() const noexcept;
        [[nodiscard]] ContentionMetrics GetMetrics() const noexcept;

    private:
        SynchronizationPrimitivesService* m_service = nullptr;
        EventResetMode m_resetMode = EventResetMode::Auto;
        mutable std::mutex m_mutex;
        std::condition_variable m_condition;
        bool m_signaled = false;
        std::atomic<u64> m_attempts = 0;
        std::atomic<u64> m_acquisitions = 0;
        std::atomic<u64> m_contentions = 0;
        std::atomic<u64> m_timeouts = 0;
        std::atomic<u64> m_totalWaitNanoseconds = 0;
        std::atomic<u64> m_maximumWaitNanoseconds = 0;
    };

    class Semaphore final
    {
    public:
        Semaphore(
            SynchronizationPrimitivesService& service,
            usize initialCount,
            usize maximumCount) noexcept;
        Semaphore(const Semaphore&) = delete;
        Semaphore& operator=(const Semaphore&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] Result<void> Release(usize count = 1) noexcept;
        [[nodiscard]] Result<WaitStatus> Acquire(
            Milliseconds timeout = Milliseconds::max()) noexcept;
        [[nodiscard]] usize Available() const noexcept;
        [[nodiscard]] ContentionMetrics GetMetrics() const noexcept;

    private:
        SynchronizationPrimitivesService* m_service = nullptr;
        mutable std::mutex m_mutex;
        std::condition_variable m_condition;
        usize m_count = 0;
        usize m_maximumCount = 0;
        bool m_valid = false;
        std::atomic<u64> m_attempts = 0;
        std::atomic<u64> m_acquisitions = 0;
        std::atomic<u64> m_contentions = 0;
        std::atomic<u64> m_timeouts = 0;
        std::atomic<u64> m_totalWaitNanoseconds = 0;
        std::atomic<u64> m_maximumWaitNanoseconds = 0;
    };

    namespace Detail
    {
        [[nodiscard]] constexpr std::memory_order ToStdMemoryOrder(
            const MemoryOrder order) noexcept
        {
            switch (order)
            {
            case MemoryOrder::Relaxed: return std::memory_order_relaxed;
            case MemoryOrder::Acquire: return std::memory_order_acquire;
            case MemoryOrder::Release: return std::memory_order_release;
            case MemoryOrder::AcquireRelease: return std::memory_order_acq_rel;
            case MemoryOrder::SequentiallyConsistent: return std::memory_order_seq_cst;
            }
            return std::memory_order_seq_cst;
        }

        [[nodiscard]] constexpr std::memory_order ToStdLoadMemoryOrder(
            const MemoryOrder order) noexcept
        {
            switch (order)
            {
            case MemoryOrder::Release: return std::memory_order_relaxed;
            case MemoryOrder::AcquireRelease: return std::memory_order_acquire;
            default: return ToStdMemoryOrder(order);
            }
        }

        [[nodiscard]] constexpr std::memory_order ToStdStoreMemoryOrder(
            const MemoryOrder order) noexcept
        {
            switch (order)
            {
            case MemoryOrder::Acquire: return std::memory_order_relaxed;
            case MemoryOrder::AcquireRelease: return std::memory_order_release;
            default: return ToStdMemoryOrder(order);
            }
        }

        [[nodiscard]] constexpr std::memory_order ToStdFailureMemoryOrder(
            const MemoryOrder order) noexcept
        {
            switch (order)
            {
            case MemoryOrder::Release: return std::memory_order_relaxed;
            case MemoryOrder::AcquireRelease: return std::memory_order_acquire;
            default: return ToStdMemoryOrder(order);
            }
        }
    }

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    class Atomic final
    {
    public:
        constexpr Atomic() noexcept = default;
        constexpr explicit Atomic(const T value) noexcept : m_value(value) {}
        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;

        [[nodiscard]] bool IsLockFree() const noexcept { return m_value.is_lock_free(); }

        [[nodiscard]] T Load(
            const MemoryOrder order = MemoryOrder::SequentiallyConsistent) const noexcept
        {
            return m_value.load(Detail::ToStdLoadMemoryOrder(order));
        }

        void Store(
            const T value,
            const MemoryOrder order = MemoryOrder::SequentiallyConsistent) noexcept
        {
            m_value.store(value, Detail::ToStdStoreMemoryOrder(order));
        }

        [[nodiscard]] T Exchange(
            const T value,
            const MemoryOrder order = MemoryOrder::SequentiallyConsistent) noexcept
        {
            return m_value.exchange(value, Detail::ToStdMemoryOrder(order));
        }

        [[nodiscard]] bool CompareExchangeStrong(
            T& expected,
            const T desired,
            const MemoryOrder success = MemoryOrder::SequentiallyConsistent,
            const MemoryOrder failure = MemoryOrder::SequentiallyConsistent) noexcept
        {
            return m_value.compare_exchange_strong(
                expected,
                desired,
                Detail::ToStdMemoryOrder(success),
                Detail::ToStdFailureMemoryOrder(failure));
        }

        [[nodiscard]] T FetchAdd(
            const T value,
            const MemoryOrder order = MemoryOrder::SequentiallyConsistent) noexcept
            requires std::is_integral_v<T>
        {
            return m_value.fetch_add(value, Detail::ToStdMemoryOrder(order));
        }

        [[nodiscard]] T FetchSub(
            const T value,
            const MemoryOrder order = MemoryOrder::SequentiallyConsistent) noexcept
            requires std::is_integral_v<T>
        {
            return m_value.fetch_sub(value, Detail::ToStdMemoryOrder(order));
        }

    private:
        std::atomic<T> m_value{};
    };
}

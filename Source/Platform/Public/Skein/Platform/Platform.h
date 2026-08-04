#pragma once

#include <Skein/Foundation/Result.h>
#include <Skein/Foundation/String.h>
#include <Skein/Foundation/Types.h>

#include <atomic>
#include <mutex>
#include <string_view>
#include <type_traits>

namespace Skein
{
    class Platform final
    {
    public:
        [[nodiscard]] static std::string_view Name() noexcept;
    };

    struct ProcessInfo final
    {
        u32 ProcessId = 0;
        u32 LogicalProcessorCount = 0;
        String ExecutablePath;
    };

    struct NativeWindowDescription final
    {
        StringView Title = "SKEIN";
        u32 Width = 1280;
        u32 Height = 720;
        bool Visible = false;
    };

    class NativeWindow final
    {
    public:
        NativeWindow() noexcept = default;
        NativeWindow(const NativeWindow&) = delete;
        NativeWindow& operator=(const NativeWindow&) = delete;
        NativeWindow(NativeWindow&& other) noexcept;
        NativeWindow& operator=(NativeWindow&& other) noexcept;
        ~NativeWindow();

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] u64 Generation() const noexcept { return m_generation; }
        [[nodiscard]] bool IsGenerationValid(u64 generation) const noexcept;
        [[nodiscard]] Result<void> Close() noexcept;

    private:
        friend class PlatformAbstractionWindowsService;
        NativeWindow(void* handle, u32 ownerThreadId, u64 generation) noexcept;

        void* m_handle = nullptr;
        u32 m_ownerThreadId = 0;
        u64 m_generation = 0;
    };

    enum class VirtualMemoryState : u8
    {
        Empty,
        Reserved,
        Committed
    };

    class VirtualMemoryRegion final
    {
    public:
        VirtualMemoryRegion() noexcept = default;
        VirtualMemoryRegion(const VirtualMemoryRegion&) = delete;
        VirtualMemoryRegion& operator=(const VirtualMemoryRegion&) = delete;
        VirtualMemoryRegion(VirtualMemoryRegion&& other) noexcept;
        VirtualMemoryRegion& operator=(VirtualMemoryRegion&& other) noexcept;
        ~VirtualMemoryRegion();

        [[nodiscard]] VirtualMemoryState State() const noexcept { return m_state; }
        [[nodiscard]] usize Size() const noexcept { return m_size; }
        [[nodiscard]] usize PageSize() const noexcept { return m_pageSize; }
        [[nodiscard]] u64 Generation() const noexcept { return m_generation; }
        [[nodiscard]] void* Data() noexcept;
        [[nodiscard]] const void* Data() const noexcept;
        [[nodiscard]] Result<void> Commit() noexcept;
        [[nodiscard]] Result<void> Decommit() noexcept;
        [[nodiscard]] Result<void> Release() noexcept;

    private:
        friend class PlatformAbstractionWindowsService;
        VirtualMemoryRegion(
            void* address,
            usize size,
            usize pageSize,
            u64 generation) noexcept;

        void* m_address = nullptr;
        usize m_size = 0;
        usize m_pageSize = 0;
        u64 m_generation = 0;
        VirtualMemoryState m_state = VirtualMemoryState::Empty;
    };

    using PlatformThreadEntry = u32 (*)(void* context) noexcept;

    class PlatformThread final
    {
    public:
        PlatformThread() noexcept = default;
        PlatformThread(const PlatformThread&) = delete;
        PlatformThread& operator=(const PlatformThread&) = delete;
        PlatformThread(PlatformThread&& other) noexcept;
        PlatformThread& operator=(PlatformThread&& other) noexcept;
        ~PlatformThread();

        [[nodiscard]] bool IsJoinable() const noexcept;
        [[nodiscard]] u32 Id() const noexcept { return m_threadId; }
        [[nodiscard]] u64 Generation() const noexcept { return m_generation; }
        [[nodiscard]] Result<u32> Join(
            Milliseconds timeout = Milliseconds::max()) noexcept;

    private:
        friend class PlatformAbstractionWindowsService;
        PlatformThread(void* handle, u32 threadId, u64 generation) noexcept;
        void CloseHandleOnly() noexcept;

        void* m_handle = nullptr;
        u32 m_threadId = 0;
        u64 m_generation = 0;
        bool m_joined = false;
    };

    class DynamicLibrarySymbol final
    {
    public:
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return m_address != nullptr;
        }

        [[nodiscard]] u64 Generation() const noexcept { return m_generation; }

    private:
        friend class DynamicLibrary;
        DynamicLibrarySymbol(void* address, u64 generation) noexcept
            : m_address(address), m_generation(generation)
        {
        }

        void* m_address = nullptr;
        u64 m_generation = 0;
    };

    class DynamicLibrary final
    {
    public:
        DynamicLibrary() noexcept = default;
        DynamicLibrary(const DynamicLibrary&) = delete;
        DynamicLibrary& operator=(const DynamicLibrary&) = delete;
        DynamicLibrary(DynamicLibrary&& other) noexcept;
        DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;
        ~DynamicLibrary();

        [[nodiscard]] bool IsLoaded() const noexcept;
        [[nodiscard]] u64 Generation() const noexcept { return m_generation; }
        [[nodiscard]] Result<DynamicLibrarySymbol> FindSymbol(StringView name) const;
        [[nodiscard]] bool IsSymbolValid(const DynamicLibrarySymbol& symbol) const noexcept;

        template<typename Function>
            requires std::is_pointer_v<Function> &&
                std::is_function_v<std::remove_pointer_t<Function>>
        [[nodiscard]] Result<Function> ResolveFunction(
            const DynamicLibrarySymbol& symbol) const noexcept
        {
            if (!IsSymbolValid(symbol))
            {
                return Unexpected{Error{
                    ErrorCode::InvalidArgument,
                    "stale dynamic library symbol"}};
            }
            return reinterpret_cast<Function>(symbol.m_address);
        }

        [[nodiscard]] Result<void> Unload() noexcept;

    private:
        friend class PlatformAbstractionWindowsService;
        DynamicLibrary(void* handle, u64 generation) noexcept;

        void* m_handle = nullptr;
        u64 m_generation = 0;
    };

    struct FileDialogDescription final
    {
        StringView Title = "Open";
    };

    struct CrashDumpDescription final
    {
        StringView Path;
    };

    struct PlatformAbstractionWindowsConfig final
    {
        bool EnableFileDialogs = true;
        bool EnableCrashDumps = true;
    };

    struct PlatformDiagnostics final
    {
        usize StartupEvents = 0;
        usize OperationEvents = 0;
        usize FailureEvents = 0;
        usize ShutdownEvents = 0;
        usize WindowsCreated = 0;
        usize ThreadsCreated = 0;
        usize LibrariesLoaded = 0;
        usize VirtualMemoryReservations = 0;
        u32 LastNativeError = 0;
        bool IsInitialised = false;
    };

    class PlatformAbstractionWindowsService final
    {
    public:
        PlatformAbstractionWindowsService() noexcept = default;
        PlatformAbstractionWindowsService(const PlatformAbstractionWindowsService&) = delete;
        PlatformAbstractionWindowsService& operator=(
            const PlatformAbstractionWindowsService&) = delete;
        ~PlatformAbstractionWindowsService();

        [[nodiscard]] Result<void> Initialise(
            const PlatformAbstractionWindowsConfig& config = {});
        void Shutdown() noexcept;

        [[nodiscard]] Result<ProcessInfo> QueryProcessInfo();
        [[nodiscard]] Result<Nanoseconds> ClockNow();
        [[nodiscard]] Result<VirtualMemoryRegion> ReserveVirtualMemory(usize size);
        [[nodiscard]] Result<PlatformThread> CreateThread(
            PlatformThreadEntry entry,
            void* context);
        [[nodiscard]] Result<DynamicLibrary> LoadDynamicLibrary(StringView path);
        [[nodiscard]] Result<NativeWindow> CreateNativeWindow(
            const NativeWindowDescription& description);
        [[nodiscard]] Result<String> ShowOpenFileDialog(
            const FileDialogDescription& description);
        [[nodiscard]] Result<void> WriteCrashDump(
            const CrashDumpDescription& description);

        [[nodiscard]] PlatformDiagnostics GetDiagnostics() const noexcept;

    private:
        [[nodiscard]] Result<void> ValidateInitialised() const noexcept;
        [[nodiscard]] Result<void> ValidateOwnerThread() const noexcept;
        void RecordOperation() noexcept;
        void RecordFailure(u32 nativeError = 0) noexcept;

        mutable std::mutex m_mutex;
        PlatformAbstractionWindowsConfig m_config{};
        PlatformDiagnostics m_diagnostics{};
        u32 m_ownerThreadId = 0;
        std::atomic<u64> m_nextGeneration = 1;
    };
}

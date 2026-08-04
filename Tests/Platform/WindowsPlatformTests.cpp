#include <Skein/Platform/Platform.h>

#include <atomic>
#include <filesystem>
#include <thread>

namespace
{
    struct ThreadContext final
    {
        std::atomic<int> Value = 0;
    };

    Skein::u32 RunPlatformThread(void* rawContext) noexcept
    {
        auto* const context = static_cast<ThreadContext*>(rawContext);
        context->Value.store(37, std::memory_order_release);
        return 73;
    }
}

int main()
{
    using namespace Skein;

    PlatformAbstractionWindowsService platform;
    PlatformAbstractionWindowsConfig config;
    config.EnableFileDialogs = false;
    if (!platform.Initialise(config) || platform.Initialise(config))
    {
        return 1;
    }
    if (Platform::Name() != "Windows x64")
    {
        return 2;
    }
    if (Platform::WindowingLibrary() != "SDL3")
    {
        return 28;
    }

    Result<ProcessInfo> processResult = platform.QueryProcessInfo();
    if (!processResult || processResult.Value().ProcessId == 0 ||
        processResult.Value().LogicalProcessorCount == 0 ||
        processResult.Value().ExecutablePath.empty())
    {
        return 3;
    }

    Result<Nanoseconds> firstClock = platform.ClockNow();
    Result<Nanoseconds> secondClock = platform.ClockNow();
    if (!firstClock || !secondClock || secondClock.Value() < firstClock.Value())
    {
        return 4;
    }

    if (platform.ReserveVirtualMemory(0))
    {
        return 5;
    }
    Result<VirtualMemoryRegion> memoryResult = platform.ReserveVirtualMemory(6000);
    if (!memoryResult)
    {
        return 6;
    }
    VirtualMemoryRegion memory = std::move(memoryResult.Value());
    const u64 memoryGeneration = memory.Generation();
    if (memory.State() != VirtualMemoryState::Reserved || memory.Size() < 6000 ||
        memory.PageSize() == 0 || memory.Data() != nullptr || !memory.Commit())
    {
        return 7;
    }
    auto* const bytes = static_cast<Byte*>(memory.Data());
    bytes[0] = Byte{0x5a};
    bytes[memory.Size() - 1] = Byte{0xa5};
    if (!memory.Decommit() || memory.Data() != nullptr || !memory.Release() ||
        memory.State() != VirtualMemoryState::Empty ||
        memory.Generation() == memoryGeneration)
    {
        return 8;
    }

    ThreadContext threadContext;
    if (platform.CreateThread(nullptr, &threadContext))
    {
        return 9;
    }
    Result<PlatformThread> threadResult = platform.CreateThread(
        RunPlatformThread,
        &threadContext);
    if (!threadResult)
    {
        return 10;
    }
    PlatformThread thread = std::move(threadResult.Value());
    Result<u32> exitCode = thread.Join(Milliseconds{5000});
    if (!exitCode || exitCode.Value() != 73 ||
        threadContext.Value.load(std::memory_order_acquire) != 37 ||
        thread.IsJoinable() || thread.Join())
    {
        return 11;
    }

    if (platform.LoadDynamicLibrary("this_library_does_not_exist.dll"))
    {
        return 12;
    }
    if (platform.GetDiagnostics().LastNativeError == 0)
    {
        return 27;
    }
    Result<DynamicLibrary> libraryResult = platform.LoadDynamicLibrary("kernel32.dll");
    if (!libraryResult)
    {
        return 13;
    }
    DynamicLibrary library = std::move(libraryResult.Value());
    Result<DynamicLibrarySymbol> symbolResult = library.FindSymbol("GetCurrentProcessId");
    if (!symbolResult || !library.IsSymbolValid(symbolResult.Value()))
    {
        return 14;
    }
    using GetProcessIdFunction = u32 (*)();
    Result<GetProcessIdFunction> function = library.ResolveFunction<GetProcessIdFunction>(
        symbolResult.Value());
    if (!function || function.Value()() != processResult.Value().ProcessId)
    {
        return 15;
    }
    DynamicLibrarySymbol staleSymbol = symbolResult.Value();
    if (!library.Unload() || library.IsLoaded() ||
        library.IsSymbolValid(staleSymbol) || library.ResolveFunction<GetProcessIdFunction>(staleSymbol))
    {
        return 16;
    }

    if (platform.CreateNativeWindow(NativeWindowDescription{"Invalid", 0, 100, false}))
    {
        return 17;
    }
    Result<NativeWindow> windowResult = platform.CreateNativeWindow(
        NativeWindowDescription{"SKEIN Platform Test", 320, 200, false});
    if (!windowResult)
    {
        return 18;
    }
    NativeWindow window = std::move(windowResult.Value());
    const u64 windowGeneration = window.Generation();
    if (!window.IsOpen() || !window.IsGenerationValid(windowGeneration) ||
        !window.Close() || window.IsOpen() || window.IsGenerationValid(windowGeneration))
    {
        return 19;
    }

    bool rejectedOtherThread = false;
    std::thread otherThread{[&platform, &rejectedOtherThread]
    {
        Result<NativeWindow> result = platform.CreateNativeWindow(
            NativeWindowDescription{"Wrong Thread", 100, 100, false});
        rejectedOtherThread = !result &&
            result.ErrorValue().Code() == ErrorCode::InvalidState;
    }};
    otherThread.join();
    if (!rejectedOtherThread)
    {
        return 20;
    }

    Result<String> dialogResult = platform.ShowOpenFileDialog({"Disabled Dialog"});
    if (dialogResult || dialogResult.ErrorValue().Code() != ErrorCode::Unsupported)
    {
        return 21;
    }

    const std::filesystem::path dumpPath =
        std::filesystem::current_path() / "SkeinPlatformChapter0110Test.dmp";
    const String dumpPathText = dumpPath.string().c_str();
    if (!platform.WriteCrashDump({dumpPathText}) ||
        !std::filesystem::exists(dumpPath) || std::filesystem::file_size(dumpPath) == 0)
    {
        return 22;
    }
    if (!std::filesystem::remove(dumpPath))
    {
        return 23;
    }
    if (platform.WriteCrashDump({}))
    {
        return 24;
    }

    Result<NativeWindow> lifetimeWindowResult = platform.CreateNativeWindow(
        NativeWindowDescription{"SKEIN Window Lifetime Test", 160, 100, false});
    if (!lifetimeWindowResult)
    {
        return 29;
    }
    NativeWindow lifetimeWindow = std::move(lifetimeWindowResult.Value());

    const PlatformDiagnostics activeDiagnostics = platform.GetDiagnostics();
    if (!activeDiagnostics.IsInitialised || activeDiagnostics.StartupEvents != 1 ||
        activeDiagnostics.OperationEvents < 8 || activeDiagnostics.FailureEvents < 7 ||
        activeDiagnostics.WindowsCreated != 2 || activeDiagnostics.ThreadsCreated != 1 ||
        activeDiagnostics.LibrariesLoaded != 1 ||
        activeDiagnostics.VirtualMemoryReservations != 1)
    {
        return 25;
    }

    platform.Shutdown();
    const PlatformDiagnostics shutdownDiagnostics = platform.GetDiagnostics();
    if (shutdownDiagnostics.IsInitialised || shutdownDiagnostics.ShutdownEvents != 1 ||
        platform.QueryProcessInfo() || !lifetimeWindow.IsOpen() || !lifetimeWindow.Close())
    {
        return 26;
    }

    return 0;
}

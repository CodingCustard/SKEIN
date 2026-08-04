#include <Skein/Platform/Platform.h>

#include <Skein/Foundation/Build.h>
#include <Skein/Foundation/Log.h>
#include <Skein/Foundation/Memory.h>

#include <Windows.h>
#include <DbgHelp.h>
#include <ShObjIdl.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <limits>
#include <memory>
#include <utility>

namespace Skein
{
    namespace
    {
        using WideString = std::basic_string<
            wchar_t,
            std::char_traits<wchar_t>,
            SkeinAllocator<wchar_t>>;

        constexpr wchar_t WindowClassName[] = L"SkeinPlatformWindowClass";

        void TracePlatform(const StringView message)
        {
#if SKEIN_ENABLE_TRACING
            Log(LogLevel::Trace, "Platform", message);
#else
            (void)message;
#endif
        }

        [[nodiscard]] Error WindowsError(
            const ErrorCode code,
            const StringView message) noexcept
        {
            return Error{code, message};
        }

        [[nodiscard]] Result<WideString> Utf8ToWide(const StringView text)
        {
            if (text.empty())
            {
                return WideString{};
            }
            if (text.size() > static_cast<usize>(std::numeric_limits<int>::max()))
            {
                return Unexpected{Error{ErrorCode::InvalidArgument, "UTF-8 input is too large"}};
            }

            const int required = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                text.data(),
                static_cast<int>(text.size()),
                nullptr,
                0);
            if (required <= 0)
            {
                return Unexpected{WindowsError(ErrorCode::InvalidArgument, "invalid UTF-8 text")};
            }

            try
            {
                WideString output(static_cast<usize>(required), L'\0');
                const int converted = MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    text.data(),
                    static_cast<int>(text.size()),
                    output.data(),
                    required);
                if (converted != required)
                {
                    return Unexpected{WindowsError(ErrorCode::Internal, "UTF-8 conversion failed")};
                }
                return output;
            }
            catch (const std::bad_alloc&)
            {
                return Unexpected{Error{ErrorCode::OutOfMemory, "UTF-8 conversion allocation failed"}};
            }
        }

        [[nodiscard]] Result<String> WideToUtf8(const wchar_t* text, const usize length)
        {
            if (length == 0)
            {
                return String{};
            }
            if (length > static_cast<usize>(std::numeric_limits<int>::max()))
            {
                return Unexpected{Error{ErrorCode::InvalidArgument, "wide input is too large"}};
            }

            const int required = WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                text,
                static_cast<int>(length),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (required <= 0)
            {
                return Unexpected{WindowsError(ErrorCode::Internal, "wide conversion failed")};
            }

            try
            {
                String output(static_cast<usize>(required), '\0');
                const int converted = WideCharToMultiByte(
                    CP_UTF8,
                    WC_ERR_INVALID_CHARS,
                    text,
                    static_cast<int>(length),
                    output.data(),
                    required,
                    nullptr,
                    nullptr);
                if (converted != required)
                {
                    return Unexpected{WindowsError(ErrorCode::Internal, "wide conversion failed")};
                }
                return output;
            }
            catch (const std::bad_alloc&)
            {
                return Unexpected{Error{ErrorCode::OutOfMemory, "wide conversion allocation failed"}};
            }
        }

        LRESULT CALLBACK SkeinWindowProcedure(
            const HWND window,
            const UINT message,
            const WPARAM wParam,
            const LPARAM lParam)
        {
            if (message == WM_CLOSE)
            {
                DestroyWindow(window);
                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        [[nodiscard]] Result<void> EnsureWindowClassRegistered()
        {
            static std::once_flag registrationFlag;
            static bool registrationSucceeded = false;
            std::call_once(registrationFlag, []
            {
                WNDCLASSEXW windowClass{};
                windowClass.cbSize = sizeof(windowClass);
                windowClass.lpfnWndProc = SkeinWindowProcedure;
                windowClass.hInstance = GetModuleHandleW(nullptr);
                windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                windowClass.lpszClassName = WindowClassName;
                registrationSucceeded = RegisterClassExW(&windowClass) != 0 ||
                    GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
            });
            if (!registrationSucceeded)
            {
                return Unexpected{WindowsError(ErrorCode::Internal, "window class registration failed")};
            }
            return {};
        }

        struct ThreadStartContext final
        {
            PlatformThreadEntry Entry = nullptr;
            void* UserContext = nullptr;
            Allocation Storage{};
        };

        DWORD WINAPI ThreadStartThunk(void* rawContext) noexcept
        {
            auto* const context = static_cast<ThreadStartContext*>(rawContext);
            const PlatformThreadEntry entry = context->Entry;
            void* const userContext = context->UserContext;
            Allocation storage = context->Storage;
            std::destroy_at(context);
            (void)GetSystemAllocator().Deallocate(storage);
            return static_cast<DWORD>(entry(userContext));
        }

        [[nodiscard]] DWORD MillisecondsToWindowsTimeout(const Milliseconds timeout) noexcept
        {
            if (timeout == Milliseconds::max())
            {
                return INFINITE;
            }
            if (timeout.count() <= 0)
            {
                return 0;
            }
            constexpr auto maximumFinite = static_cast<Milliseconds::rep>(INFINITE - 1);
            return static_cast<DWORD>(std::min(timeout.count(), maximumFinite));
        }
    }

    std::string_view Platform::Name() noexcept
    {
        return "Windows x64";
    }

    NativeWindow::NativeWindow(
        void* const handle,
        const u32 ownerThreadId,
        const u64 generation) noexcept
        : m_handle(handle),
          m_ownerThreadId(ownerThreadId),
          m_generation(generation)
    {
    }

    NativeWindow::NativeWindow(NativeWindow&& other) noexcept
        : m_handle(std::exchange(other.m_handle, nullptr)),
          m_ownerThreadId(std::exchange(other.m_ownerThreadId, 0)),
          m_generation(std::exchange(other.m_generation, 0))
    {
    }

    NativeWindow& NativeWindow::operator=(NativeWindow&& other) noexcept
    {
        if (this != &other)
        {
            (void)Close();
            m_handle = std::exchange(other.m_handle, nullptr);
            m_ownerThreadId = std::exchange(other.m_ownerThreadId, 0);
            m_generation = std::exchange(other.m_generation, 0);
        }
        return *this;
    }

    NativeWindow::~NativeWindow()
    {
        (void)Close();
    }

    bool NativeWindow::IsOpen() const noexcept
    {
        return m_handle != nullptr && IsWindow(static_cast<HWND>(m_handle)) != FALSE;
    }

    bool NativeWindow::IsGenerationValid(const u64 generation) const noexcept
    {
        return IsOpen() && generation != 0 && generation == m_generation;
    }

    Result<void> NativeWindow::Close() noexcept
    {
        if (m_handle == nullptr)
        {
            return {};
        }
        if (GetCurrentThreadId() != m_ownerThreadId)
        {
            return Unexpected{Error{ErrorCode::InvalidState, "window closed from non-owner thread"}};
        }
        if (IsWindow(static_cast<HWND>(m_handle)) != FALSE &&
            DestroyWindow(static_cast<HWND>(m_handle)) == FALSE)
        {
            return Unexpected{WindowsError(ErrorCode::Internal, "window destruction failed")};
        }
        m_handle = nullptr;
        m_ownerThreadId = 0;
        ++m_generation;
        return {};
    }

    VirtualMemoryRegion::VirtualMemoryRegion(
        void* const address,
        const usize size,
        const usize pageSize,
        const u64 generation) noexcept
        : m_address(address),
          m_size(size),
          m_pageSize(pageSize),
          m_generation(generation),
          m_state(VirtualMemoryState::Reserved)
    {
    }

    VirtualMemoryRegion::VirtualMemoryRegion(VirtualMemoryRegion&& other) noexcept
        : m_address(std::exchange(other.m_address, nullptr)),
          m_size(std::exchange(other.m_size, 0)),
          m_pageSize(std::exchange(other.m_pageSize, 0)),
          m_generation(std::exchange(other.m_generation, 0)),
          m_state(std::exchange(other.m_state, VirtualMemoryState::Empty))
    {
    }

    VirtualMemoryRegion& VirtualMemoryRegion::operator=(VirtualMemoryRegion&& other) noexcept
    {
        if (this != &other)
        {
            (void)Release();
            m_address = std::exchange(other.m_address, nullptr);
            m_size = std::exchange(other.m_size, 0);
            m_pageSize = std::exchange(other.m_pageSize, 0);
            m_generation = std::exchange(other.m_generation, 0);
            m_state = std::exchange(other.m_state, VirtualMemoryState::Empty);
        }
        return *this;
    }

    VirtualMemoryRegion::~VirtualMemoryRegion()
    {
        (void)Release();
    }

    void* VirtualMemoryRegion::Data() noexcept
    {
        return m_state == VirtualMemoryState::Committed ? m_address : nullptr;
    }

    const void* VirtualMemoryRegion::Data() const noexcept
    {
        return m_state == VirtualMemoryState::Committed ? m_address : nullptr;
    }

    Result<void> VirtualMemoryRegion::Commit() noexcept
    {
        if (m_state != VirtualMemoryState::Reserved)
        {
            return Unexpected{Error{ErrorCode::InvalidState, "virtual memory is not reserved"}};
        }
        if (VirtualAlloc(m_address, m_size, MEM_COMMIT, PAGE_READWRITE) == nullptr)
        {
            return Unexpected{WindowsError(ErrorCode::OutOfMemory, "virtual memory commit failed")};
        }
        m_state = VirtualMemoryState::Committed;
        return {};
    }

    Result<void> VirtualMemoryRegion::Decommit() noexcept
    {
        if (m_state != VirtualMemoryState::Committed)
        {
            return Unexpected{Error{ErrorCode::InvalidState, "virtual memory is not committed"}};
        }
        if (VirtualFree(m_address, m_size, MEM_DECOMMIT) == FALSE)
        {
            return Unexpected{WindowsError(ErrorCode::Internal, "virtual memory decommit failed")};
        }
        m_state = VirtualMemoryState::Reserved;
        return {};
    }

    Result<void> VirtualMemoryRegion::Release() noexcept
    {
        if (m_address == nullptr)
        {
            return {};
        }
        if (VirtualFree(m_address, 0, MEM_RELEASE) == FALSE)
        {
            return Unexpected{WindowsError(ErrorCode::Internal, "virtual memory release failed")};
        }
        m_address = nullptr;
        m_size = 0;
        m_pageSize = 0;
        m_state = VirtualMemoryState::Empty;
        ++m_generation;
        return {};
    }

    PlatformThread::PlatformThread(
        void* const handle,
        const u32 threadId,
        const u64 generation) noexcept
        : m_handle(handle),
          m_threadId(threadId),
          m_generation(generation)
    {
    }

    PlatformThread::PlatformThread(PlatformThread&& other) noexcept
        : m_handle(std::exchange(other.m_handle, nullptr)),
          m_threadId(std::exchange(other.m_threadId, 0)),
          m_generation(std::exchange(other.m_generation, 0)),
          m_joined(std::exchange(other.m_joined, false))
    {
    }

    PlatformThread& PlatformThread::operator=(PlatformThread&& other) noexcept
    {
        if (this != &other)
        {
            if (IsJoinable())
            {
                (void)Join();
            }
            CloseHandleOnly();
            m_handle = std::exchange(other.m_handle, nullptr);
            m_threadId = std::exchange(other.m_threadId, 0);
            m_generation = std::exchange(other.m_generation, 0);
            m_joined = std::exchange(other.m_joined, false);
        }
        return *this;
    }

    PlatformThread::~PlatformThread()
    {
        if (IsJoinable())
        {
            (void)Join();
        }
        CloseHandleOnly();
    }

    bool PlatformThread::IsJoinable() const noexcept
    {
        return m_handle != nullptr && !m_joined;
    }

    Result<u32> PlatformThread::Join(const Milliseconds timeout) noexcept
    {
        if (!IsJoinable())
        {
            return Unexpected{Error{ErrorCode::InvalidState, "thread is not joinable"}};
        }
        if (GetCurrentThreadId() == m_threadId)
        {
            return Unexpected{Error{ErrorCode::InvalidState, "thread cannot join itself"}};
        }

        const DWORD waitResult = WaitForSingleObject(
            static_cast<HANDLE>(m_handle),
            MillisecondsToWindowsTimeout(timeout));
        if (waitResult == WAIT_TIMEOUT)
        {
            return Unexpected{Error{ErrorCode::Timeout, "thread join timed out"}};
        }
        if (waitResult != WAIT_OBJECT_0)
        {
            return Unexpected{WindowsError(ErrorCode::Internal, "thread join failed")};
        }

        DWORD exitCode = 0;
        if (GetExitCodeThread(static_cast<HANDLE>(m_handle), &exitCode) == FALSE)
        {
            return Unexpected{WindowsError(ErrorCode::Internal, "thread exit code query failed")};
        }
        m_joined = true;
        return static_cast<u32>(exitCode);
    }

    void PlatformThread::CloseHandleOnly() noexcept
    {
        if (m_handle != nullptr)
        {
            CloseHandle(static_cast<HANDLE>(m_handle));
            m_handle = nullptr;
            m_threadId = 0;
            ++m_generation;
        }
    }

    DynamicLibrary::DynamicLibrary(void* const handle, const u64 generation) noexcept
        : m_handle(handle),
          m_generation(generation)
    {
    }

    DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
        : m_handle(std::exchange(other.m_handle, nullptr)),
          m_generation(std::exchange(other.m_generation, 0))
    {
    }

    DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
    {
        if (this != &other)
        {
            (void)Unload();
            m_handle = std::exchange(other.m_handle, nullptr);
            m_generation = std::exchange(other.m_generation, 0);
        }
        return *this;
    }

    DynamicLibrary::~DynamicLibrary()
    {
        (void)Unload();
    }

    bool DynamicLibrary::IsLoaded() const noexcept
    {
        return m_handle != nullptr;
    }

    Result<DynamicLibrarySymbol> DynamicLibrary::FindSymbol(const StringView name) const
    {
        if (!IsLoaded())
        {
            return Unexpected{Error{ErrorCode::InvalidState, "library is not loaded"}};
        }
        if (name.empty() || name.find('\0') != StringView::npos)
        {
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid library symbol name"}};
        }

        try
        {
            const String nullTerminated{name};
            const FARPROC symbol = GetProcAddress(
                static_cast<HMODULE>(m_handle),
                nullTerminated.c_str());
            if (symbol == nullptr)
            {
                return Unexpected{Error{ErrorCode::NotFound, "library symbol not found"}};
            }
            return DynamicLibrarySymbol{reinterpret_cast<void*>(symbol), m_generation};
        }
        catch (const std::bad_alloc&)
        {
            return Unexpected{Error{ErrorCode::OutOfMemory, "symbol name allocation failed"}};
        }
    }

    bool DynamicLibrary::IsSymbolValid(const DynamicLibrarySymbol& symbol) const noexcept
    {
        return IsLoaded() && symbol.m_address != nullptr &&
            symbol.m_generation == m_generation;
    }

    Result<void> DynamicLibrary::Unload() noexcept
    {
        if (m_handle == nullptr)
        {
            return {};
        }
        if (FreeLibrary(static_cast<HMODULE>(m_handle)) == FALSE)
        {
            return Unexpected{WindowsError(ErrorCode::Internal, "library unload failed")};
        }
        m_handle = nullptr;
        ++m_generation;
        return {};
    }

    PlatformAbstractionWindowsService::~PlatformAbstractionWindowsService()
    {
        Shutdown();
    }

    Result<void> PlatformAbstractionWindowsService::Initialise(
        const PlatformAbstractionWindowsConfig& config)
    {
        std::scoped_lock lock{m_mutex};
        if (m_diagnostics.IsInitialised)
        {
            return Unexpected{Error{ErrorCode::InvalidState, "platform service is already initialised"}};
        }
        if (sizeof(void*) != 8)
        {
            return Unexpected{Error{ErrorCode::Unsupported, "Windows x64 is required"}};
        }
        m_config = config;
        m_diagnostics = {};
        m_diagnostics.IsInitialised = true;
        ++m_diagnostics.StartupEvents;
        m_ownerThreadId = GetCurrentThreadId();
        m_nextGeneration.store(1, std::memory_order_relaxed);
        TracePlatform("Windows platform service initialised");
        return {};
    }

    void PlatformAbstractionWindowsService::Shutdown() noexcept
    {
        std::scoped_lock lock{m_mutex};
        if (!m_diagnostics.IsInitialised)
        {
            return;
        }
        m_diagnostics.IsInitialised = false;
        ++m_diagnostics.ShutdownEvents;
        m_ownerThreadId = 0;
        TracePlatform("Windows platform service shutdown");
    }

    Result<ProcessInfo> PlatformAbstractionWindowsService::QueryProcessInfo()
    {
        Result<void> state = ValidateInitialised();
        if (!state)
        {
            RecordFailure();
            return Unexpected{state.ErrorValue()};
        }

        wchar_t path[32768]{};
        DWORD pathLength = static_cast<DWORD>(std::size(path));
        if (QueryFullProcessImageNameW(GetCurrentProcess(), 0, path, &pathLength) == FALSE)
        {
            RecordFailure(GetLastError());
            return Unexpected{WindowsError(ErrorCode::Internal, "process path query failed")};
        }
        Result<String> executablePath = WideToUtf8(path, pathLength);
        if (!executablePath)
        {
            RecordFailure();
            return Unexpected{executablePath.ErrorValue()};
        }

        SYSTEM_INFO systemInfo{};
        GetNativeSystemInfo(&systemInfo);
        RecordOperation();
        return ProcessInfo{
            GetCurrentProcessId(),
            systemInfo.dwNumberOfProcessors,
            std::move(executablePath.Value())};
    }

    Result<Nanoseconds> PlatformAbstractionWindowsService::ClockNow()
    {
        Result<void> state = ValidateInitialised();
        if (!state)
        {
            RecordFailure();
            return Unexpected{state.ErrorValue()};
        }

        LARGE_INTEGER frequency{};
        LARGE_INTEGER counter{};
        if (QueryPerformanceFrequency(&frequency) == FALSE || frequency.QuadPart <= 0 ||
            QueryPerformanceCounter(&counter) == FALSE)
        {
            RecordFailure(GetLastError());
            return Unexpected{WindowsError(ErrorCode::Internal, "high resolution clock query failed")};
        }

        const long double nanoseconds =
            static_cast<long double>(counter.QuadPart) * 1'000'000'000.0L /
            static_cast<long double>(frequency.QuadPart);
        if (nanoseconds > static_cast<long double>(std::numeric_limits<i64>::max()))
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::Internal, "high resolution clock overflow"}};
        }
        RecordOperation();
        return Nanoseconds{static_cast<i64>(nanoseconds)};
    }

    Result<VirtualMemoryRegion> PlatformAbstractionWindowsService::ReserveVirtualMemory(
        const usize size)
    {
        Result<void> state = ValidateInitialised();
        if (!state)
        {
            RecordFailure();
            return Unexpected{state.ErrorValue()};
        }
        if (size == 0)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "virtual memory size is zero"}};
        }

        SYSTEM_INFO systemInfo{};
        GetNativeSystemInfo(&systemInfo);
        const usize pageSize = systemInfo.dwPageSize;
        if (size > std::numeric_limits<usize>::max() - (pageSize - 1))
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "virtual memory size overflow"}};
        }
        const usize roundedSize = (size + pageSize - 1) & ~(pageSize - 1);
        void* const address = VirtualAlloc(nullptr, roundedSize, MEM_RESERVE, PAGE_NOACCESS);
        if (address == nullptr)
        {
            RecordFailure(GetLastError());
            return Unexpected{WindowsError(ErrorCode::OutOfMemory, "virtual memory reserve failed")};
        }

        const u64 generation = m_nextGeneration.fetch_add(1, std::memory_order_relaxed);
        {
            std::scoped_lock lock{m_mutex};
            ++m_diagnostics.VirtualMemoryReservations;
        }
        RecordOperation();
        return VirtualMemoryRegion{address, roundedSize, pageSize, generation};
    }

    Result<PlatformThread> PlatformAbstractionWindowsService::CreateThread(
        const PlatformThreadEntry entry,
        void* const context)
    {
        Result<void> state = ValidateInitialised();
        if (!state)
        {
            RecordFailure();
            return Unexpected{state.ErrorValue()};
        }
        if (entry == nullptr)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "thread entry is null"}};
        }

        Result<Allocation> contextStorage = GetSystemAllocator().Allocate({
            sizeof(ThreadStartContext),
            alignof(ThreadStartContext),
            MemoryTag::Persistent});
        if (!contextStorage)
        {
            RecordFailure();
            return Unexpected{contextStorage.ErrorValue()};
        }
        Allocation storage = contextStorage.Value();
        auto* const startContext = std::construct_at(
            static_cast<ThreadStartContext*>(storage.Data),
            ThreadStartContext{entry, context, storage});

        DWORD threadId = 0;
        HANDLE const threadHandle = ::CreateThread(
            nullptr,
            0,
            ThreadStartThunk,
            startContext,
            0,
            &threadId);
        if (threadHandle == nullptr)
        {
            const DWORD nativeError = GetLastError();
            std::destroy_at(startContext);
            (void)GetSystemAllocator().Deallocate(storage);
            RecordFailure(nativeError);
            return Unexpected{WindowsError(ErrorCode::Internal, "thread creation failed")};
        }

        const u64 generation = m_nextGeneration.fetch_add(1, std::memory_order_relaxed);
        {
            std::scoped_lock lock{m_mutex};
            ++m_diagnostics.ThreadsCreated;
        }
        RecordOperation();
        return PlatformThread{threadHandle, threadId, generation};
    }

    Result<DynamicLibrary> PlatformAbstractionWindowsService::LoadDynamicLibrary(
        const StringView path)
    {
        Result<void> state = ValidateInitialised();
        if (!state)
        {
            RecordFailure();
            return Unexpected{state.ErrorValue()};
        }
        if (path.empty() || path.find('\0') != StringView::npos)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid library path"}};
        }
        Result<WideString> widePath = Utf8ToWide(path);
        if (!widePath)
        {
            RecordFailure();
            return Unexpected{widePath.ErrorValue()};
        }
        HMODULE const library = LoadLibraryExW(
            widePath.Value().c_str(),
            nullptr,
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (library == nullptr)
        {
            RecordFailure(GetLastError());
            return Unexpected{Error{ErrorCode::NotFound, "dynamic library could not be loaded"}};
        }

        const u64 generation = m_nextGeneration.fetch_add(1, std::memory_order_relaxed);
        {
            std::scoped_lock lock{m_mutex};
            ++m_diagnostics.LibrariesLoaded;
        }
        RecordOperation();
        return DynamicLibrary{library, generation};
    }

    Result<NativeWindow> PlatformAbstractionWindowsService::CreateNativeWindow(
        const NativeWindowDescription& description)
    {
        Result<void> owner = ValidateOwnerThread();
        if (!owner)
        {
            RecordFailure();
            return Unexpected{owner.ErrorValue()};
        }
        if (description.Width == 0 || description.Height == 0 ||
            description.Width > static_cast<u32>(std::numeric_limits<LONG>::max()) ||
            description.Height > static_cast<u32>(std::numeric_limits<LONG>::max()) ||
            description.Title.find('\0') != StringView::npos)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid window description"}};
        }
        Result<void> windowClassResult = EnsureWindowClassRegistered();
        if (!windowClassResult)
        {
            RecordFailure(GetLastError());
            return Unexpected{windowClassResult.ErrorValue()};
        }
        Result<WideString> title = Utf8ToWide(description.Title);
        if (!title)
        {
            RecordFailure();
            return Unexpected{title.ErrorValue()};
        }

        const DWORD style = WS_OVERLAPPEDWINDOW;
        RECT rectangle{
            0,
            0,
            static_cast<LONG>(description.Width),
            static_cast<LONG>(description.Height)};
        if (AdjustWindowRect(&rectangle, style, FALSE) == FALSE)
        {
            RecordFailure(GetLastError());
            return Unexpected{WindowsError(ErrorCode::Internal, "window rectangle calculation failed")};
        }
        HWND const window = CreateWindowExW(
            0,
            WindowClassName,
            title.Value().c_str(),
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rectangle.right - rectangle.left,
            rectangle.bottom - rectangle.top,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (window == nullptr)
        {
            RecordFailure(GetLastError());
            return Unexpected{WindowsError(ErrorCode::Internal, "window creation failed")};
        }
        if (description.Visible)
        {
            ShowWindow(window, SW_SHOW);
        }

        const u64 generation = m_nextGeneration.fetch_add(1, std::memory_order_relaxed);
        {
            std::scoped_lock lock{m_mutex};
            ++m_diagnostics.WindowsCreated;
        }
        RecordOperation();
        return NativeWindow{window, GetCurrentThreadId(), generation};
    }

    Result<String> PlatformAbstractionWindowsService::ShowOpenFileDialog(
        const FileDialogDescription& description)
    {
        Result<void> owner = ValidateOwnerThread();
        if (!owner)
        {
            RecordFailure();
            return Unexpected{owner.ErrorValue()};
        }
        if (!m_config.EnableFileDialogs)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::Unsupported, "file dialogs are disabled"}};
        }
        if (description.Title.empty() || description.Title.find('\0') != StringView::npos)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid file dialog description"}};
        }

        const HRESULT initialiseResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool shouldUninitialise = initialiseResult == S_OK || initialiseResult == S_FALSE;
        if (FAILED(initialiseResult) && initialiseResult != RPC_E_CHANGED_MODE)
        {
            RecordFailure(static_cast<u32>(initialiseResult));
            return Unexpected{Error{ErrorCode::Internal, "COM initialisation failed"}};
        }

        IFileOpenDialog* dialog = nullptr;
        HRESULT result = CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog));
        if (FAILED(result))
        {
            if (shouldUninitialise)
            {
                CoUninitialize();
            }
            RecordFailure(static_cast<u32>(result));
            return Unexpected{Error{ErrorCode::Internal, "file dialog creation failed"}};
        }

        Result<WideString> title = Utf8ToWide(description.Title);
        if (!title)
        {
            dialog->Release();
            if (shouldUninitialise)
            {
                CoUninitialize();
            }
            RecordFailure();
            return Unexpected{title.ErrorValue()};
        }
        if (title)
        {
            result = dialog->SetTitle(title.Value().c_str());
        }
        if (SUCCEEDED(result))
        {
            result = dialog->Show(nullptr);
        }
        if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        {
            dialog->Release();
            if (shouldUninitialise)
            {
                CoUninitialize();
            }
            RecordFailure(static_cast<u32>(result));
            return Unexpected{Error{ErrorCode::Cancelled, "file dialog cancelled"}};
        }
        if (FAILED(result))
        {
            dialog->Release();
            if (shouldUninitialise)
            {
                CoUninitialize();
            }
            RecordFailure(static_cast<u32>(result));
            return Unexpected{Error{ErrorCode::Internal, "file dialog failed"}};
        }

        IShellItem* item = nullptr;
        result = dialog->GetResult(&item);
        PWSTR selectedPath = nullptr;
        if (SUCCEEDED(result))
        {
            result = item->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath);
        }

        Result<String> converted = FAILED(result)
            ? Result<String>{Unexpected{Error{ErrorCode::Internal, "selected path query failed"}}}
            : WideToUtf8(selectedPath, std::wcslen(selectedPath));
        CoTaskMemFree(selectedPath);
        if (item != nullptr)
        {
            item->Release();
        }
        dialog->Release();
        if (shouldUninitialise)
        {
            CoUninitialize();
        }
        if (!converted)
        {
            RecordFailure(FAILED(result) ? static_cast<u32>(result) : 0);
            return Unexpected{converted.ErrorValue()};
        }
        RecordOperation();
        return std::move(converted.Value());
    }

    Result<void> PlatformAbstractionWindowsService::WriteCrashDump(
        const CrashDumpDescription& description)
    {
        Result<void> state = ValidateInitialised();
        if (!state)
        {
            RecordFailure();
            return state;
        }
        if (!m_config.EnableCrashDumps)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::Unsupported, "crash dumps are disabled"}};
        }
        if (description.Path.empty() || description.Path.find('\0') != StringView::npos)
        {
            RecordFailure();
            return Unexpected{Error{ErrorCode::InvalidArgument, "invalid crash dump path"}};
        }
        Result<WideString> path = Utf8ToWide(description.Path);
        if (!path)
        {
            RecordFailure();
            return Unexpected{path.ErrorValue()};
        }

        HANDLE const file = CreateFileW(
            path.Value().c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            RecordFailure(GetLastError());
            return Unexpected{Error{ErrorCode::InputOutput, "crash dump file creation failed"}};
        }

        static std::mutex dumpMutex;
        BOOL written = FALSE;
        {
            std::scoped_lock dumpLock{dumpMutex};
            written = MiniDumpWriteDump(
                GetCurrentProcess(),
                GetCurrentProcessId(),
                file,
                MiniDumpNormal,
                nullptr,
                nullptr,
                nullptr);
        }
        CloseHandle(file);
        if (written == FALSE)
        {
            const DWORD nativeError = GetLastError();
            DeleteFileW(path.Value().c_str());
            RecordFailure(nativeError);
            return Unexpected{Error{ErrorCode::InputOutput, "crash dump write failed"}};
        }
        RecordOperation();
        return {};
    }

    PlatformDiagnostics PlatformAbstractionWindowsService::GetDiagnostics() const noexcept
    {
        std::scoped_lock lock{m_mutex};
        return m_diagnostics;
    }

    Result<void> PlatformAbstractionWindowsService::ValidateInitialised() const noexcept
    {
        std::scoped_lock lock{m_mutex};
        if (!m_diagnostics.IsInitialised)
        {
            return Unexpected{Error{ErrorCode::InvalidState, "platform service is not initialised"}};
        }
        return {};
    }

    Result<void> PlatformAbstractionWindowsService::ValidateOwnerThread() const noexcept
    {
        std::scoped_lock lock{m_mutex};
        if (!m_diagnostics.IsInitialised)
        {
            return Unexpected{Error{ErrorCode::InvalidState, "platform service is not initialised"}};
        }
        if (GetCurrentThreadId() != m_ownerThreadId)
        {
            return Unexpected{Error{ErrorCode::InvalidState, "platform operation requires owner thread"}};
        }
        return {};
    }

    void PlatformAbstractionWindowsService::RecordOperation() noexcept
    {
        std::scoped_lock lock{m_mutex};
        ++m_diagnostics.OperationEvents;
        TracePlatform("Windows platform operation succeeded");
    }

    void PlatformAbstractionWindowsService::RecordFailure(const u32 nativeError) noexcept
    {
        std::scoped_lock lock{m_mutex};
        ++m_diagnostics.FailureEvents;
        m_diagnostics.LastNativeError = nativeError;
        TracePlatform("Windows platform operation failed");
    }
}

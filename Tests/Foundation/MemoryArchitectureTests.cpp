#include <Skein/Foundation/Arena.h>
#include <Skein/Foundation/FlatMap.h>
#include <Skein/Foundation/Pool.h>
#include <Skein/Foundation/SmallVector.h>

#include <cstdint>
#include <array>
#include <functional>
#include <thread>
#include <utility>

namespace
{
    [[nodiscard]] bool IsAligned(const void* pointer, const Skein::usize alignment)
    {
        return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
    }
}

int main()
{
    using namespace Skein;

    MemoryArchitectureService memory;
    MemoryArchitectureConfig config;
    config.TrackedAllocationCapacity = 64;
    config.Budgets[static_cast<usize>(MemoryTag::Persistent)].Limit = 8192;
    config.Budgets[static_cast<usize>(MemoryTag::Transient)].Limit = 8192;
    config.Budgets[static_cast<usize>(MemoryTag::Frame)].Limit = 2048;
    config.Budgets[static_cast<usize>(MemoryTag::Scratch)].Limit = 2048;
    if (!memory.Initialise(config) || memory.Initialise(config))
    {
        return 1;
    }

    Result<Allocation> alignedResult = memory.Allocate({
        128,
        64,
        MemoryTag::Persistent});
    if (!alignedResult || !IsAligned(alignedResult.Value().Data, 64))
    {
        return 2;
    }
    Allocation aligned = alignedResult.Value();
    Allocation stale = aligned;
    if (!memory.Deallocate(aligned) || aligned || memory.Deallocate(stale))
    {
        return 3;
    }

    Result<Allocation> overBudget = memory.Allocate({
        9000,
        alignof(std::max_align_t),
        MemoryTag::Persistent});
    Result<Allocation> badAlignment = memory.Allocate({
        32,
        3,
        MemoryTag::Transient});
    if (overBudget || overBudget.ErrorValue().Code() != ErrorCode::OutOfMemory ||
        badAlignment || badAlignment.ErrorValue().Code() != ErrorCode::InvalidArgument)
    {
        return 4;
    }

    std::array<bool, 4> sharedAllocatorResults{};
    std::array<std::thread, 4> workers;
    for (usize workerIndex = 0; workerIndex < workers.size(); ++workerIndex)
    {
        workers[workerIndex] = std::thread{
            [&memory, &sharedAllocatorResults, workerIndex]
            {
                bool succeeded = true;
                for (usize iteration = 0; iteration < 8; ++iteration)
                {
                    Result<Allocation> result = memory.Allocate({
                        32,
                        16,
                        MemoryTag::Transient});
                    if (!result)
                    {
                        succeeded = false;
                        break;
                    }
                    Allocation block = result.Value();
                    if (!memory.Deallocate(block))
                    {
                        succeeded = false;
                        break;
                    }
                }
                sharedAllocatorResults[workerIndex] = succeeded;
            }};
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }
    for (const bool result : sharedAllocatorResults)
    {
        if (!result)
        {
            return 25;
        }
    }

    LinearArena arena;
    if (!arena.Initialise(memory, 512, MemoryTag::Transient, 64))
    {
        return 5;
    }
    Result<Allocation> arenaFirstResult = arena.Allocate({
        37,
        32,
        MemoryTag::Transient});
    const ArenaMarker marker = arena.Mark();
    Result<Allocation> arenaSecondResult = arena.Allocate({
        100,
        64,
        MemoryTag::Transient});
    if (!arenaFirstResult || !arenaSecondResult ||
        !IsAligned(arenaFirstResult.Value().Data, 32) ||
        !IsAligned(arenaSecondResult.Value().Data, 64))
    {
        return 6;
    }
    Allocation arenaSecond = arenaSecondResult.Value();
    if (!arena.Restore(marker) || arena.Deallocate(arenaSecond))
    {
        return 7;
    }
    Result<Allocation> arenaExhausted = arena.Allocate({
        600,
        8,
        MemoryTag::Transient});
    if (arenaExhausted || arenaExhausted.ErrorValue().Code() != ErrorCode::OutOfMemory)
    {
        return 8;
    }

    bool rejectedOtherThread = false;
    std::thread otherThread{[&arena, &rejectedOtherThread]
    {
        Result<Allocation> result = arena.Allocate({
            8,
            8,
            MemoryTag::Transient});
        rejectedOtherThread = !result &&
            result.ErrorValue().Code() == ErrorCode::InvalidState;
    }};
    otherThread.join();
    if (!rejectedOtherThread)
    {
        return 9;
    }

    FixedBlockPool pool;
    if (!pool.Initialise(memory, 48, 3, 16, MemoryTag::Persistent))
    {
        return 10;
    }
    Result<Allocation> poolOneResult = pool.Allocate({24, 8, MemoryTag::Persistent});
    Result<Allocation> poolTwoResult = pool.Allocate({24, 8, MemoryTag::Persistent});
    Result<Allocation> poolThreeResult = pool.Allocate({24, 8, MemoryTag::Persistent});
    Result<Allocation> poolFullResult = pool.Allocate({24, 8, MemoryTag::Persistent});
    if (!poolOneResult || !poolTwoResult || !poolThreeResult || poolFullResult ||
        pool.GetDiagnostics().PeakActiveBlocks != 3)
    {
        return 11;
    }
    Allocation poolOne = poolOneResult.Value();
    Allocation stalePoolOne = poolOne;
    if (!pool.Deallocate(poolOne) || pool.Deallocate(stalePoolOne))
    {
        return 12;
    }

    FrameAllocator frame;
    if (!frame.Initialise(memory, 512, 64))
    {
        return 13;
    }
    Result<Allocation> frameBlockResult = frame.Allocate({64, 32, MemoryTag::Frame});
    if (!frameBlockResult)
    {
        return 14;
    }
    Allocation previousFrameBlock = frameBlockResult.Value();
    if (!frame.BeginFrame() || frame.FrameIndex() != 1 ||
        frame.Deallocate(previousFrameBlock))
    {
        return 15;
    }

    ThreadScratchAllocator& scratch = GetThreadScratchAllocator();
    if (!scratch.Initialise(memory, 512, 64))
    {
        return 16;
    }
    const usize scratchStart = scratch.GetDiagnostics().UsedBytes;
    {
        ScratchScope scope{scratch};
        Result<Allocation> scratchBlock = scratch.Allocate({
            96,
            32,
            MemoryTag::Scratch});
        if (!scratchBlock || scratch.GetDiagnostics().UsedBytes <= scratchStart)
        {
            return 17;
        }
    }
    if (scratch.GetDiagnostics().UsedBytes != scratchStart)
    {
        return 18;
    }

    {
        using IntAllocator = SkeinAllocator<int>;
        SmallVector<int, 1, IntAllocator> values{
            IntAllocator{memory, MemoryTag::Transient}};
        if (!values.PushBack(1) || !values.PushBack(2) || values.IsInline())
        {
            return 19;
        }

        using Entry = std::pair<int, int>;
        using EntryAllocator = SkeinAllocator<Entry>;
        FlatMap<int, int, std::less<int>, EntryAllocator> map{
            EntryAllocator{memory, MemoryTag::Persistent}};
        if (!map.InsertOrAssign(2, 20) || !map.InsertOrAssign(1, 10) ||
            map.Find(2) == nullptr || *map.Find(2) != 20)
        {
            return 20;
        }
    }

    const MemoryDiagnostics activeDiagnostics = memory.GetDiagnostics();
    if (!activeDiagnostics.IsInitialised || activeDiagnostics.StartupEvents != 1 ||
        activeDiagnostics.OperationEvents == 0 || activeDiagnostics.FailureEvents < 3 ||
        activeDiagnostics.Tags[static_cast<usize>(MemoryTag::Persistent)].PeakBytes == 0 ||
        activeDiagnostics.Tags[static_cast<usize>(MemoryTag::Transient)].PeakBytes == 0 ||
        activeDiagnostics.Tags[static_cast<usize>(MemoryTag::Frame)].PeakBytes == 0 ||
        activeDiagnostics.Tags[static_cast<usize>(MemoryTag::Scratch)].PeakBytes == 0)
    {
        return 21;
    }

    scratch.Shutdown();
    frame.Shutdown();
    pool.Shutdown();
    arena.Shutdown();

    Result<Allocation> leakedResult = memory.Allocate({
        32,
        16,
        MemoryTag::Persistent});
    if (!leakedResult)
    {
        return 22;
    }
    memory.Shutdown();

    const MemoryDiagnostics shutdownDiagnostics = memory.GetDiagnostics();
    if (shutdownDiagnostics.IsInitialised || shutdownDiagnostics.ShutdownEvents != 1 ||
        shutdownDiagnostics.LeakedAllocations != 1 ||
        shutdownDiagnostics.LeakedBytes < 32)
    {
        return 23;
    }
    if (memory.Allocate({16, 8, MemoryTag::Persistent}))
    {
        return 24;
    }

    return 0;
}

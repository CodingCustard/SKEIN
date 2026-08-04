#include <Skein/Foundation/Hash.h>
#include <Skein/Foundation/Name.h>
#include <Skein/Foundation/Path.h>
#include <Skein/Foundation/String.h>
#include <Skein/Foundation/Uuid.h>

#include <atomic>
#include <string_view>
#include <thread>

int main()
{
    using namespace Skein;

    if (!IsValidUtf8("SKEIN \xE2\x98\x85") || IsValidUtf8("\xC0\xAF"))
    {
        return 1;
    }

    Result<NormalizedPath> drivePath = NormalizePath("e:\\Skein\\.\\Source\\..\\Assets//mesh.bin");
    if (!drivePath || !drivePath.Value().IsAbsolute() ||
        drivePath.Value().View() != "E:/Skein/Assets/mesh.bin")
    {
        return 2;
    }

    Result<NormalizedPath> relativePath = NormalizePath("Content/Characters/../Props");
    if (!relativePath || relativePath.Value().IsAbsolute() ||
        relativePath.Value().View() != "Content/Props")
    {
        return 3;
    }

    if (NormalizePath("C:/../escape"))
    {
        return 4;
    }

    constexpr u64 expectedHash = 0x8bb48b9bfd8fa995ULL;
    if (StableHash64("SKEIN") != expectedHash)
    {
        return 5;
    }

    Result<Uuid> uuid = Uuid::Parse("123e4567-e89b-12d3-a456-426614174000");
    if (!uuid || !uuid.Value().IsValid())
    {
        return 6;
    }

    const UuidText uuidText = uuid.Value().ToString();
    if (std::string_view{uuidText.data(), 36} != "123e4567-e89b-12d3-a456-426614174000" ||
        Uuid::Parse("not-a-uuid"))
    {
        return 7;
    }

    NameTable names;
    Result<Name> first = names.Intern("Renderer.Opaque");
    Result<Name> second = names.Intern("Renderer.Opaque");
    if (!first || !second || first.Value() != second.Value() || names.Size() != 1)
    {
        return 8;
    }

    Result<String> resolved = names.Resolve(first.Value());
    if (!resolved || resolved.Value() != "Renderer.Opaque")
    {
        return 9;
    }

    std::atomic<bool> concurrentSuccess = true;
    const auto internSharedName = [&names, &concurrentSuccess]()
    {
        if (!names.Intern("Renderer.Shared"))
        {
            concurrentSuccess.store(false, std::memory_order_relaxed);
        }
    };
    std::thread workerA{internSharedName};
    std::thread workerB{internSharedName};
    std::thread workerC{internSharedName};
    std::thread workerD{internSharedName};
    workerA.join();
    workerB.join();
    workerC.join();
    workerD.join();

    if (!concurrentSuccess.load(std::memory_order_relaxed) || names.Size() != 2)
    {
        return 10;
    }

    names.Clear();
    if (names.Size() != 0 || names.Resolve(first.Value()))
    {
        return 11;
    }

    return 0;
}

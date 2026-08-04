#include <Skein/Foundation/Assert.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace Skein
{
    namespace
    {
        std::atomic<AssertionHandler> g_handler = nullptr;

        [[nodiscard]] constexpr std::string_view KindName(const AssertionKind kind) noexcept
        {
            switch (kind)
            {
            case AssertionKind::Assert: return "Assert";
            case AssertionKind::Verify: return "Verify";
            case AssertionKind::Ensure: return "Ensure";
            case AssertionKind::Fatal:  return "Fatal";
            }

            return "Unknown";
        }

        [[nodiscard]] AssertionAction DefaultHandler(
            const AssertionFailure& failure) noexcept
        {
            const std::string_view kind = KindName(failure.Kind);
            std::fprintf(
                stderr,
                "[%.*s] %.*s: %.*s (%s:%u)\n",
                static_cast<int>(kind.size()),
                kind.data(),
                static_cast<int>(failure.Expression.size()),
                failure.Expression.data(),
                static_cast<int>(failure.Message.size()),
                failure.Message.data(),
                failure.Source.file_name(),
                failure.Source.line());
            std::fflush(stderr);

            return failure.Kind == AssertionKind::Ensure
                ? AssertionAction::Continue
                : AssertionAction::Terminate;
        }
    }

    AssertionHandler SetAssertionHandler(const AssertionHandler handler) noexcept
    {
        return g_handler.exchange(handler, std::memory_order_acq_rel);
    }

    void ResetAssertionHandler() noexcept
    {
        g_handler.store(nullptr, std::memory_order_release);
    }

    bool ReportAssertionFailure(
        const AssertionKind kind,
        const std::string_view expression,
        const std::string_view message,
        const std::source_location source) noexcept
    {
        const AssertionFailure failure{kind, expression, message, source};
        const AssertionHandler handler = g_handler.load(std::memory_order_acquire);
        const AssertionAction action = handler != nullptr
            ? handler(failure)
            : DefaultHandler(failure);

        if (action == AssertionAction::Terminate)
        {
            std::abort();
        }

        return false;
    }
}

#include <Skein/Foundation/Assert.h>
#include <Skein/Foundation/Error.h>
#include <Skein/Foundation/Result.h>
#include <Skein/Foundation/Types.h>

#include <string>
#include <string_view>

namespace
{
    Skein::u32 g_failureCount = 0;
    Skein::AssertionKind g_lastKind = Skein::AssertionKind::Assert;

    Skein::AssertionAction CaptureFailure(
        const Skein::AssertionFailure& failure) noexcept
    {
        ++g_failureCount;
        g_lastKind = failure.Kind;
        return Skein::AssertionAction::Continue;
    }

    [[nodiscard]] Skein::Result<Skein::u32> PositiveValue(const Skein::i32 value)
    {
        if (value <= 0)
        {
            return Skein::Unexpected{Skein::Error{
                Skein::ErrorCode::InvalidArgument,
                "value must be positive"}};
        }

        return static_cast<Skein::u32>(value);
    }

    [[nodiscard]] Skein::Result<void> PropagatePositive(const Skein::i32 value)
    {
        SKEIN_TRY(PositiveValue(value));
        return {};
    }
}

int main()
{
    using namespace Skein;

    std::string transientMessage = "owned error message";
    const Error ownedError{ErrorCode::InvalidState, transientMessage};
    transientMessage.assign("changed");

    if (ownedError.Code() != ErrorCode::InvalidState ||
        ownedError.Message() != std::string_view{"owned error message"} ||
        ownedError.Source().line() == 0)
    {
        return 1;
    }

    Result<u32> success = PositiveValue(7);
    if (!success || success.Value() != 7)
    {
        return 2;
    }

    Result<u32> failure = PositiveValue(-1);
    if (failure || failure.ErrorValue().Code() != ErrorCode::InvalidArgument)
    {
        return 3;
    }

    const Result<void> propagated = PropagatePositive(-1);
    if (propagated || propagated.ErrorValue().Code() != ErrorCode::InvalidArgument)
    {
        return 4;
    }

    const AssertionHandler previousHandler = SetAssertionHandler(&CaptureFailure);
    if (previousHandler != nullptr)
    {
        return 5;
    }

    const bool ensured = SKEIN_ENSURE_MESSAGE(false, "injected ensure");
    const bool verified = SKEIN_VERIFY_MESSAGE(false, "injected verify");
    if (ensured || verified || g_failureCount != 2 ||
        g_lastKind != AssertionKind::Verify)
    {
        ResetAssertionHandler();
        return 6;
    }

    const bool fatal = SKEIN_FATAL("injected fatal");
    if (fatal || g_failureCount != 3 || g_lastKind != AssertionKind::Fatal)
    {
        ResetAssertionHandler();
        return 7;
    }

#if SKEIN_ENABLE_ASSERTS
    const bool asserted = SKEIN_ASSERT_MESSAGE(false, "injected assert");
    if (asserted || g_failureCount != 4 || g_lastKind != AssertionKind::Assert)
    {
        ResetAssertionHandler();
        return 8;
    }
#endif

    ResetAssertionHandler();
    return 0;
}

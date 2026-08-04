#pragma once

#include <Skein/Foundation/BuildConfig.h>
#include <Skein/Foundation/Types.h>

#include <source_location>
#include <string_view>

namespace Skein
{
    enum class AssertionKind : u8
    {
        Assert,
        Verify,
        Ensure,
        Fatal
    };

    enum class AssertionAction : u8
    {
        Continue,
        Terminate
    };

    struct AssertionFailure final
    {
        AssertionKind Kind;
        std::string_view Expression;
        std::string_view Message;
        std::source_location Source;
    };

    using AssertionHandler = AssertionAction (*)(const AssertionFailure&) noexcept;

    [[nodiscard]] AssertionHandler SetAssertionHandler(AssertionHandler handler) noexcept;
    void ResetAssertionHandler() noexcept;

    [[nodiscard]] bool ReportAssertionFailure(
        AssertionKind kind,
        std::string_view expression,
        std::string_view message,
        std::source_location source = std::source_location::current()) noexcept;
}

#if SKEIN_ENABLE_ASSERTS
#define SKEIN_ASSERT(expression) \
    ((expression) ? true : ::Skein::ReportAssertionFailure( \
        ::Skein::AssertionKind::Assert, #expression, {}, std::source_location::current()))
#define SKEIN_ASSERT_MESSAGE(expression, message) \
    ((expression) ? true : ::Skein::ReportAssertionFailure( \
        ::Skein::AssertionKind::Assert, #expression, message, std::source_location::current()))
#else
#define SKEIN_ASSERT(expression) true
#define SKEIN_ASSERT_MESSAGE(expression, message) true
#endif

#define SKEIN_VERIFY(expression) \
    ((expression) ? true : ::Skein::ReportAssertionFailure( \
        ::Skein::AssertionKind::Verify, #expression, {}, std::source_location::current()))
#define SKEIN_VERIFY_MESSAGE(expression, message) \
    ((expression) ? true : ::Skein::ReportAssertionFailure( \
        ::Skein::AssertionKind::Verify, #expression, message, std::source_location::current()))
#define SKEIN_ENSURE(expression) \
    ((expression) ? true : ::Skein::ReportAssertionFailure( \
        ::Skein::AssertionKind::Ensure, #expression, {}, std::source_location::current()))
#define SKEIN_ENSURE_MESSAGE(expression, message) \
    ((expression) ? true : ::Skein::ReportAssertionFailure( \
        ::Skein::AssertionKind::Ensure, #expression, message, std::source_location::current()))
#define SKEIN_FATAL(message) \
    (::Skein::ReportAssertionFailure( \
        ::Skein::AssertionKind::Fatal, "fatal", message, std::source_location::current()))

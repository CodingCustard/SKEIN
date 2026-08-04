#pragma once

#include <Skein/Foundation/Assert.h>
#include <Skein/Foundation/Error.h>

#include <concepts>
#include <type_traits>
#include <utility>
#include <variant>

namespace Skein
{
    struct Unexpected final
    {
        Error Value;
    };

    template<typename T>
    class [[nodiscard]] Result final
    {
        static_assert(!std::is_reference_v<T>);
        static_assert(!std::same_as<T, void>);

    public:
        Result(const T& value)
            requires std::copy_constructible<T>
            : m_storage(std::in_place_index<0>, value)
        {
        }

        Result(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
            : m_storage(std::in_place_index<0>, std::move(value))
        {
        }

        Result(const Unexpected& unexpected)
            : m_storage(std::in_place_index<1>, unexpected.Value)
        {
        }

        Result(Unexpected&& unexpected) noexcept
            : m_storage(std::in_place_index<1>, std::move(unexpected.Value))
        {
        }

        [[nodiscard]] bool HasValue() const noexcept
        {
            return m_storage.index() == 0;
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return HasValue();
        }

        [[nodiscard]] T& Value() & noexcept
        {
            (void)SKEIN_VERIFY_MESSAGE(HasValue(), "Result does not contain a value");
            return std::get<0>(m_storage);
        }

        [[nodiscard]] const T& Value() const& noexcept
        {
            (void)SKEIN_VERIFY_MESSAGE(HasValue(), "Result does not contain a value");
            return std::get<0>(m_storage);
        }

        [[nodiscard]] T&& Value() && noexcept
        {
            (void)SKEIN_VERIFY_MESSAGE(HasValue(), "Result does not contain a value");
            return std::get<0>(std::move(m_storage));
        }

        [[nodiscard]] const Error& ErrorValue() const noexcept
        {
            (void)SKEIN_VERIFY_MESSAGE(!HasValue(), "Result does not contain an error");
            return std::get<1>(m_storage);
        }

    private:
        std::variant<T, Error> m_storage;
    };

    template<>
    class [[nodiscard]] Result<void> final
    {
    public:
        constexpr Result() noexcept = default;

        constexpr Result(const Unexpected& unexpected) noexcept
            : m_error(unexpected.Value)
        {
        }

        constexpr Result(Unexpected&& unexpected) noexcept
            : m_error(std::move(unexpected.Value))
        {
        }

        [[nodiscard]] constexpr bool HasValue() const noexcept
        {
            return !m_error.IsFailure();
        }

        [[nodiscard]] explicit constexpr operator bool() const noexcept
        {
            return HasValue();
        }

        [[nodiscard]] const Error& ErrorValue() const noexcept
        {
            (void)SKEIN_VERIFY_MESSAGE(!HasValue(), "Result does not contain an error");
            return m_error;
        }

    private:
        Error m_error;
    };
}

#define SKEIN_TRY(expression) \
    do \
    { \
        auto skeinTryResult = (expression); \
        if (!skeinTryResult) \
        { \
            return ::Skein::Unexpected{skeinTryResult.ErrorValue()}; \
        } \
    } while (false)

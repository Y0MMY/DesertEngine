#pragma once

#include <optional>
#include <variant>
#include <string>
#include <type_traits>

#include <spdlog/fmt/fmt.h>

#include <utility>

namespace Common
{
    template <typename T>
    class ResultStr;

    template <typename T = bool>
    ResultStr<T> MakeError( const std::string& message );

    template <typename T = bool, typename... Args>
    ResultStr<T> MakeFormattedError( fmt::format_string<Args...> format, Args&&... args );

    template <typename T = bool>
    auto MakeSuccess( T&& value );

    using BoolResultStr = ResultStr<bool>;

    template <typename T>
    class ResultStr
    {
    public:
        class Error
        {
        public:
            explicit Error( const std::string& errorMessage ) : m_ErrorMessage( errorMessage )
            {
            }

            // The format string is checked by the COMPILER (see Common/Core/Logger.hpp for the full why).
            // This constructor builds the message of a FAILURE, so a mismatched brace count here threw
            // `fmt::format_error` at the one moment the code was already handling something going wrong -
            // and nothing catches it. A genuinely runtime format must say so with `fmt::runtime(...)`.
            //
            // The `const std::string&` overload above still wins for a single non-literal argument, so a
            // message that merely CONTAINS braces and formats nothing is unaffected, exactly as before.
            template <typename... Args>
            explicit Error( fmt::format_string<Args...> format, Args&&... args )
                 : m_ErrorMessage( fmt::format( format, std::forward<Args>( args )... ) )
            {
            }

            const std::string& GetMessage() const
            {
                return m_ErrorMessage;
            }

        private:
            std::string m_ErrorMessage;
        };

    public:
        /**
         * @brief Default constructor creates an error state.
         */
        ResultStr() : m_Outcome( Error( "Uninitialized Result" ) ), m_IsSuccess( false )
        {
        }

        bool IsSuccess() const
        {
            return m_IsSuccess;
        }

        const T& GetValue() const
        {
            if ( !m_IsSuccess )
            {
                // NOTE: This is dangerous for non-POD types, but T is often shared_ptr or bool
                static T empty{};
                return empty;
            }
            return std::get<T>( m_Outcome );
        }

        T ExtractValue()
        {
            if ( !m_IsSuccess )
            {
                return T{};
            }
            return std::move( std::get<T>( m_Outcome ) );
        }

        std::string GetError() const
        {
            if ( m_IsSuccess )
            {
                return s_NoError;
            }
            return std::get<Error>( m_Outcome ).GetMessage();
        }

        explicit operator bool() const
        {
            return m_IsSuccess;
        }

    private:
        /**
         * @brief Constructor for Success state.
         */
        template <typename U, typename = std::enable_if_t<!std::is_same_v<std::decay_t<U>, Error>>>
        explicit ResultStr( U&& value ) : m_Outcome( std::forward<U>( value ) ), m_IsSuccess( true )
        {
        }

        /**
         * @brief Constructor for Error state.
         */
        explicit ResultStr( Error&& error ) : m_Outcome( std::move( error ) ), m_IsSuccess( false )
        {
        }

        static inline std::string s_NoError = "No error";
        std::variant<T, Error>    m_Outcome;
        bool                      m_IsSuccess = false;

    private:
        template <typename U>
        friend ResultStr<U> MakeError( const std::string& message );

        template <typename U>
        friend auto MakeSuccess( U&& value );

        template <typename U, typename... Args>
        friend ResultStr<U> MakeFormattedError( fmt::format_string<Args...> format, Args&&... args );
    };

    template <typename T>
    ResultStr<T> MakeError( const std::string& message )
    {
        return ResultStr<T>( typename ResultStr<T>::Error( message ) );
    }

    // THE FORMAT STRING HAS TO STAY A FORMAT STRING ALL THE WAY DOWN. This took `std::string&&`, which
    // erased its compile-time nature at exactly this boundary: every call site below it was writing a
    // literal, and every one of them lost its checking here. That is why the whole conversion showed up as
    // errors on this one line - the template was instantiating the checked constructor with a runtime
    // string, from every caller in the engine at once.
    template <typename T, typename... Args>
    ResultStr<T> MakeFormattedError( fmt::format_string<Args...> format, Args&&... args )
    {
        return ResultStr<T>( typename ResultStr<T>::Error( format, std::forward<Args>( args )... ) );
    }

    template <typename T>
    auto MakeSuccess( T&& value )
    {
        using ValueType = std::decay_t<T>;
        return ResultStr<ValueType>( std::forward<T>( value ) );
    }

} // namespace Common

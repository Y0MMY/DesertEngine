#pragma once

#include <optional>
#include <variant>
#include <string>

#include <spdlog/fmt/fmt.h>

namespace Common
{
    template <typename T>
    class Result;

    template <typename T>
    Result<T> MakeErrorWithCode( T&& errorCode, const std::string& message );

    template <typename T, typename... Args>
    Result<T> MakeFormattedErrorWithCode( T&& errorCode, std::string_view format, Args&&... args );

    template <typename T = bool>
    Result<T> MakeError( const std::string& message );

    template <typename T = bool, typename... Args>
    Result<T> MakeFormattedError( std::string_view format, Args&&... args );

    template <typename T = bool>
    auto MakeSuccess( const T& value );

    using BoolResult = Result<bool>;

    template <typename T>
    class Result
    {
    public:
        Result() = default;
    public:
        class Error
        {
        public:
            explicit Error( T&& errorCode, const std::string& errorMessage )
                 : m_ErrorMessage( errorMessage ), m_ErrorCode( errorCode )
            {
            }

            explicit Error( const std::string& errorMessage )
                 : m_ErrorMessage( errorMessage ), m_ErrorCode( std::nullopt )
            {
            }

            template <typename... Args>
            explicit Error( T&& errorCode, std::string_view format, Args&&... args )
                 : m_ErrorMessage( fmt::vformat( format, fmt::make_format_args( args... ) ) ),
                   m_ErrorCode( errorCode )

            {
            }

            template <typename... Args>
            explicit Error( std::string_view format, Args&&... args )
                 : m_ErrorMessage( fmt::vformat( format, fmt::make_format_args( args... ) ) ),
                   m_ErrorCode( std::nullopt )

            {
            }

            const std::string& GetMessage() const
            {
                return m_ErrorMessage;
            }

            const auto& GetErrorCode() const
            {
                return m_ErrorCode;
            }

        private:
            std::string      m_ErrorMessage;
            std::optional<T> m_ErrorCode;
        };

        bool IsSuccess() const
        {
            return m_IsSuccess;
        }

        const T GetValue() const
        {
            if ( !m_IsSuccess )
            {
                return T{};
            }
            return std::get<T>( m_Outcome );
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
        explicit Result( const Error& error ) : m_Outcome( error ), m_IsSuccess( false )
        {
        }

        explicit Result( const T& value ) : m_Outcome( value ), m_IsSuccess( true )
        {
        }

        static inline std::string s_NoError = "Cannot get error message, result is a success";
        std::variant<T, Error>    m_Outcome;
        bool                      m_IsSuccess;

    private:
        friend Result<T> MakeErrorWithCode<T>( T&& errorCode, const std::string& message );
        friend Result<T> MakeError<T>( const std::string& message );

        friend auto MakeSuccess<T>( const T& value );

        template <typename U, typename... Args>
        friend Result<U> MakeFormattedErrorWithCode( T&& errorCode, std::string_view format, Args&&... args );

        template <typename U, typename... Args>
        friend Result<U> MakeFormattedError( std::string_view format, Args&&... args );
    };

    template <typename T>
    Result<T> MakeErrorWithCode( T&& errorCode, const std::string& message )
    {
        return Result<T>( Result<T>::Error( errorCode, message ) );
    }

    template <typename T>
    Result<T> MakeError( const std::string& message )
    {
        return Result<T>( Result<T>::Error( message ) );
    }

    template <typename T, typename... Args>
    Result<T> MakeFormattedErrorWithCode( T&& errorCode, std::string_view format, Args&&... args )
    {
        return Result<T>( Result<T>::Error( errorCode, format, std::forward<Args>( args )... ) );
    }

    template <typename T, typename... Args>
    Result<T> MakeFormattedError( std::string_view format, Args&&... args )
    {
        return Result<T>( Result<T>::Error( format, std::forward<Args>( args )... ) );
    }

    template <typename T>
    auto MakeSuccess( const T& value )
    {
        return Result<T>( value );
    }

} // namespace Common
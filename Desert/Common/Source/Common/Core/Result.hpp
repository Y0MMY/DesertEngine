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
    Result<T> MakeError( const std::string& message );

    template <typename T, typename... Args>
    Result<T> MakeFormattedError( std::string_view format, Args&&... args );

    template <typename T>
    auto MakeSuccess( const T& value );

    using BoolResult = Result<bool>;

    template <typename T>
    class Result
    {
    public:
        class Error
        {
        public:
            explicit Error( const std::string& errorMessage ) : m_ErrorMessage( errorMessage )
            {
            }

            template <typename... Args>
            Error( std::string_view format, Args&&... args )
                 : m_ErrorMessage( fmt::vformat( format, fmt::make_format_args( args... ) ) )

            {
            }

            const std::string& GetMessage() const
            {
                return m_ErrorMessage;
            }

        private:
            std::string m_ErrorMessage;
        };

        bool IsSuccess() const
        {
            return m_IsSuccess;
        }

        const std::optional<T> GetValue() const
        {
            if ( !m_IsSuccess )
            {
                return std::nullopt;
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
        friend Result<T> MakeError<T>( const std::string& message );
        friend auto      MakeSuccess<T>( const T& value );

        template <typename U, typename... Args>
        friend Result<U> MakeFormattedError( std::string_view format, Args&&... args );
    };

    template <typename T>
    Result<T> MakeError( const std::string& message )
    {
        return Result<T>( Result<T>::Error( message ) );
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
#pragma once

#include <optional>
#include <variant>
#include <string>
#include <vector>
#include <initializer_list>

#include <spdlog/fmt/fmt.h>

namespace Common
{
    template <typename T, typename ErrorCodeType = int>
    class ResultWithCodes;

    template <typename T, typename ErrorCodeType = int>
    ResultWithCodes<T, ErrorCodeType> MakeErrorWithCodes( std::initializer_list<ErrorCodeType> errorCodes,
                                                          const std::string&                   message );

    template <typename T, typename ErrorCodeType = int, typename... Args>
    ResultWithCodes<T, ErrorCodeType> MakeFormattedErrorWithCodes( std::initializer_list<ErrorCodeType> errorCodes,
                                                                   std::string_view format, Args&&... args );
    template <typename T = bool, typename ErrorCodeType = int>
    auto MakeSuccessWithCodes( const T& value );

    // Alias for common use case
    template <typename ErrorCodeType = int>
    using BoolResultWithCodes = ResultWithCodes<bool, ErrorCodeType>;

    template <typename T, typename ErrorCodeType>
    class ResultWithCodes
    {
    public:
        ResultWithCodes() = default;

    public:
        class Error
        {
        public:
            explicit Error( std::initializer_list<ErrorCodeType> errorCodes, const std::string& errorMessage )
                 : m_ErrorMessage( errorMessage ), m_ErrorCodes( errorCodes )
            {
            }

            explicit Error( const std::string& errorMessage ) : m_ErrorMessage( errorMessage )
            {
            }

            template <typename... Args>
            explicit Error( std::initializer_list<ErrorCodeType> errorCodes, std::string_view format,
                            Args&&... args )
                 : m_ErrorMessage( fmt::vformat( format, fmt::make_format_args( args... ) ) ),
                   m_ErrorCodes( errorCodes )
            {
            }

            template <typename... Args>
            explicit Error( std::string_view format, Args&&... args )
                 : m_ErrorMessage( fmt::vformat( format, fmt::make_format_args( args... ) ) )
            {
            }

            const std::string& GetMessage() const
            {
                return m_ErrorMessage;
            }

            const std::vector<ErrorCodeType>& GetErrorCodes() const
            {
                return m_ErrorCodes;
            }

            bool HasErrorCode( ErrorCodeType code ) const
            {
                return std::find( m_ErrorCodes.begin(), m_ErrorCodes.end(), code ) != m_ErrorCodes.end();
            }

            bool HasAnyErrorCode( std::initializer_list<ErrorCodeType> codes ) const
            {
                for ( const auto& code : codes )
                {
                    if ( HasErrorCode( code ) )
                    {
                        return true;
                    }
                }
                return false;
            }

            bool HasAllErrorCodes( std::initializer_list<ErrorCodeType> codes ) const
            {
                for ( const auto& code : codes )
                {
                    if ( !HasErrorCode( code ) )
                    {
                        return false;
                    }
                }
                return true;
            }

        private:
            std::string                m_ErrorMessage;
            std::vector<ErrorCodeType> m_ErrorCodes;
        };

        bool IsSuccess() const
        {
            return m_IsSuccess;
        }

        const T& GetValue() const
        {
            if ( !m_IsSuccess )
            {
                static T default_value{};
                return default_value;
            }
            return std::get<T>( m_Outcome );
        }

        T& GetValue()
        {
            if ( !m_IsSuccess )
            {
                static T default_value{};
                return default_value;
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

        const std::vector<ErrorCodeType>& GetErrorCodes() const
        {
            if ( m_IsSuccess )
            {
                static std::vector<ErrorCodeType> empty_codes;
                return empty_codes;
            }
            return std::get<Error>( m_Outcome ).GetErrorCodes();
        }

        bool HasErrorCode( ErrorCodeType code ) const
        {
            if ( m_IsSuccess )
            {
                return false;
            }
            return std::get<Error>( m_Outcome ).HasErrorCode( code );
        }

        bool HasAnyErrorCode( std::initializer_list<ErrorCodeType> codes ) const
        {
            if ( m_IsSuccess )
            {
                return false;
            }
            return std::get<Error>( m_Outcome ).HasAnyErrorCode( codes );
        }

        bool HasAllErrorCodes( std::initializer_list<ErrorCodeType> codes ) const
        {
            if ( m_IsSuccess )
            {
                return false;
            }
            return std::get<Error>( m_Outcome ).HasAllErrorCodes( codes );
        }

        explicit operator bool() const
        {
            return m_IsSuccess;
        }

        // For compatibility with existing code
        const ErrorCodeType GetPrimaryErrorCode() const
        {
            if ( m_IsSuccess || GetErrorCodes().empty() )
            {
                return ErrorCodeType{};
            }
            return GetErrorCodes()[0];
        }

    private:
        explicit ResultWithCodes( const Error& error ) : m_Outcome( error ), m_IsSuccess( false )
        {
        }

        explicit ResultWithCodes( const T& value ) : m_Outcome( value ), m_IsSuccess( true )
        {
        }

        static inline std::string s_NoError = "Cannot get error message, result is a success";
        std::variant<T, Error>    m_Outcome;
        bool                      m_IsSuccess = false;

    private:
        template <typename U, typename E>
        friend ResultWithCodes<U, E> MakeErrorWithCodes<U, E>( std::initializer_list<E> errorCodes,
                                                               const std::string&       message );

        template <typename U, typename E>
        friend auto MakeSuccessWithCodes<U, E>( const U& value );

        template <typename U, typename E, typename... Args>
        friend ResultWithCodes<U, E> MakeFormattedErrorWithCodes<U, E>( std::initializer_list<E> errorCodes,
                                                                        std::string_view format, Args&&... args );
    };

    template <typename T, typename ErrorCodeType>
    ResultWithCodes<T, ErrorCodeType> MakeErrorWithCodes( std::initializer_list<ErrorCodeType> errorCodes,
                                                          const std::string&                   message )
    {
        return ResultWithCodes<T, ErrorCodeType>(
             typename ResultWithCodes<T, ErrorCodeType>::Error( errorCodes, message ) );
    }

    template <typename T, typename ErrorCodeType, typename... Args>
    ResultWithCodes<T, ErrorCodeType> MakeFormattedErrorWithCodes( std::initializer_list<ErrorCodeType> errorCodes,
                                                                   std::string_view format, Args&&... args )
    {
        return ResultWithCodes<T, ErrorCodeType>( typename ResultWithCodes<T, ErrorCodeType>::Error(
             errorCodes, format, std::forward<Args>( args )... ) );
    }

    template <typename T, typename ErrorCodeType>
    auto MakeSuccessWithCodes( const T& value )
    {
        return ResultWithCodes<T, ErrorCodeType>( value );
    }

    template <typename T, typename ErrorCodeType = int>
    ResultWithCodes<T, ErrorCodeType> MakeErrorWithSingleCode( ErrorCodeType      errorCode,
                                                               const std::string& message )
    {
        return MakeErrorWithCodes<T, ErrorCodeType>( { errorCode }, message );
    }

    template <typename T, typename ErrorCodeType = int, typename... Args>
    ResultWithCodes<T, ErrorCodeType> MakeFormattedErrorWithSingleCode( ErrorCodeType    errorCode,
                                                                        std::string_view format, Args&&... args )
    {
        return MakeFormattedErrorWithCodes<T, ErrorCodeType>( { errorCode }, format,
                                                              std::forward<Args>( args )... );
    }

} // namespace Common
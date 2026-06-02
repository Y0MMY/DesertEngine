#pragma once

#include <optional>
#include <variant>
#include <string>
#include <type_traits>

#include <spdlog/fmt/fmt.h>

namespace Common
{
    template <typename T>
    class ResultStr;

    template <typename T = bool>
    ResultStr<T> MakeError( const std::string& message );

    template <typename T = bool, typename... Args>
    ResultStr<T> MakeFormattedError( std::string&& format, Args&&... args );

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

            template <typename... Args>
            explicit Error( std::string_view format, Args&&... args )
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
        friend ResultStr<U> MakeFormattedError( std::string&& format, Args&&... args );
    };

    template <typename T>
    ResultStr<T> MakeError( const std::string& message )
    {
        return ResultStr<T>( typename ResultStr<T>::Error( message ) );
    }

    template <typename T, typename... Args>
    ResultStr<T> MakeFormattedError( std::string&& format, Args&&... args )
    {
        return ResultStr<T>( typename ResultStr<T>::Error( std::move( format ), std::forward<Args>( args )... ) );
    }

    template <typename T>
    auto MakeSuccess( T&& value )
    {
        using ValueType = std::decay_t<T>;
        return ResultStr<ValueType>( std::forward<T>( value ) );
    }

} // namespace Common

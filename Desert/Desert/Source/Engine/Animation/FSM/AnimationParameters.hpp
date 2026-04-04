#pragma once

#include <unordered_map>
#include <string>
#include <variant>

namespace Desert::Animation
{
    class AnimationParameters
    {
    public:
        using ParamValue = std::variant<bool, int, float>;

        void Set( const std::string& name, ParamValue value )
        {
            m_Params[name] = std::move( value );
        }

        template <typename T>
        T Get( const std::string& name ) const
        {
            return std::get<T>( m_Params.at( name ) );
        }

        bool Has( const std::string& name ) const
        {
            return m_Params.find( name ) != m_Params.end();
        }

    private:
        std::unordered_map<std::string, ParamValue> m_Params;
    };
} // namespace Desert::Animation
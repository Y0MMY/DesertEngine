#pragma once

#include <Engine/Core/Models/Shader.hpp>
#include <Common/Core/Memory/Buffer.hpp>

namespace Desert::Graphic
{
    class FieldProperty
    {
    public:
        FieldProperty( const Core::Models::Common::Field& field ) : m_Field( field ), m_Dirty( false )
        {
            m_LocalData.Allocate( field.Size );
        }

        template <typename T>
        bool SetValue( const T& value )
        {
            static_assert( std::is_standard_layout_v<T>, "T must be standard layout" );
            if ( sizeof( T ) != m_Field.Size && m_Field.ArraySize == 0 )
            {
                return false;
            }

            memcpy( m_LocalData.Data, &value, sizeof( T ) );
            m_Dirty = true;
            return true;
        }

        template <typename T>
        bool SetArray( const T* data, uint32_t count )
        {
            static_assert( std::is_standard_layout_v<T>, "T must be standard layout" );
            if ( count > m_Field.ArraySize )
            {
                return false;
            }

            memcpy( m_LocalData.Data, data, sizeof( T ) * count );
            m_Dirty = true;
            return true;
        }

        template <typename T>
        T GetValue() const
        {
            static_assert( std::is_standard_layout_v<T>, "T must be standard layout" );
            T value;
            memcpy( &value, m_LocalData.Data, sizeof( T ) );
            return value;
        }

        template <typename T>
        std::vector<T> GetArray( uint32_t count ) const
        {
            static_assert( std::is_standard_layout_v<T>, "T must be standard layout" );
            std::vector<T> result( count );
            memcpy( result.data(), m_LocalData.Data, sizeof( T ) * count );
            return result;
        }

        void MarkDirty()
        {
            m_Dirty = true;
        }
        bool IsDirty() const
        {
            return m_Dirty;
        }

        const Core::Models::Common::Field& GetFieldInfo() const
        {
            return m_Field;
        }

        const Common::Memory::Buffer& GetLocalData() const
        {
            return m_LocalData;
        }

    private:
        Core::Models::Common::Field m_Field;
        Common::Memory::Buffer      m_LocalData;
        bool                        m_Dirty;
    };
} // namespace Desert::Graphic
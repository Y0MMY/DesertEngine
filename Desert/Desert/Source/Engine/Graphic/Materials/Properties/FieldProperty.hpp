#pragma once

#include <Engine/ShaderResources/ShaderReflectionTypes.hpp>
#include <Common/Core/Memory/Buffer.hpp>

namespace Desert::Graphic
{
    class FieldProperty
    {
    public:
        FieldProperty( const ShaderResources::ShaderLayout::ShaderFieldLayout& field )
             : m_Field( field ), m_DirtyCount( 3 )
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
            m_DirtyCount = 3;
            return true;
        }

        bool SetRawBytes( const void* data, size_t size )
        {
            if ( size > m_Field.Size )
                return false;
            memcpy( m_LocalData.Data, data, size );
            m_DirtyCount = 3;
            return true;
        }

        template <typename T>
        bool SetArray( const T* data, uint32_t count )
        {
            static_assert( std::is_standard_layout_v<T>, "T must be standard layout" );
            if ( count != m_Field.ArraySize )
            {
                DESERT_VERIFY( false, "" );
                return false;
            }

            memcpy( m_LocalData.Data, data, sizeof( T ) * count );
            m_DirtyCount = 3;
            return true;
        }

        [[nodiscard]] bool IsArray() const
        {
            return m_Field.ArraySize > 1U;
        }

        [[nodiscard]] uint32_t GetArraySize() const
        {
            return m_Field.ArraySize;
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
            m_DirtyCount = 3;
        }
        void MarkClean()
        {
            if ( m_DirtyCount > 0 )
            {
                m_DirtyCount--;
            }
        }
        bool IsDirty() const
        {
            return m_DirtyCount > 0;
        }

        const ShaderResources::ShaderLayout::ShaderFieldLayout& GetFieldInfo() const
        {
            return m_Field;
        }

        const Common::Memory::Buffer& GetLocalData() const
        {
            return m_LocalData;
        }

    private:
        ShaderResources::ShaderLayout::ShaderFieldLayout m_Field;
        Common::Memory::Buffer                           m_LocalData;
        uint32_t                                         m_DirtyCount;
    };
} // namespace Desert::Graphic
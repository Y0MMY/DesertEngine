#pragma once

#include <Engine/Core/EngineContext.hpp>

#include <array>

#include <Engine/ShaderResources/ShaderReflectionTypes.hpp>
#include <Engine/Graphic/Materials/Properties/PropertyDirty.hpp>
#include <Common/Core/Memory/Buffer.hpp>

namespace Desert::Graphic
{
    class FieldProperty
    {
    public:
        FieldProperty( const ShaderResources::ShaderLayout::ShaderFieldLayout& field ) : m_Field( field )
        {
            m_DirtyCount.fill( PropertyDirty::DirtyLifetime() );
            m_LastCleanFrame.fill( PropertyDirty::kNeverCleaned );
            m_LocalData.Allocate( field.Size );
        }

        static uint32_t ActiveSlot()
        {
            const uint32_t slot = EngineContext::GetInstance().GetActiveRendererSlot();
            return slot < Engine::kMaxRendererSlots ? slot : 0;
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
            MarkDirty(); // every slot owes itself this write
            return true;
        }

        bool SetRawBytes( const void* data, size_t size )
        {
            if ( size > m_Field.Size )
                return false;
            memcpy( m_LocalData.Data, data, size );
            MarkDirty(); // every slot owes itself this write
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
            MarkDirty(); // every slot owes itself this write
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

        // Per RENDERER SLOT, like MaterialProperty and for the same reason: the uniform buffer this field
        // lands in has a copy per (frame x slot), and a field cleaned by the view that is recording would
        // otherwise never be written into the other view's copy.
        void MarkDirty()
        {
            m_DirtyCount.fill( PropertyDirty::DirtyLifetime() );
        }
        void MarkClean()
        {
            // Clean at most once per frame so the dirty window spans frames-in-flight distinct frames,
            // even when the owning uniform buffer is flushed multiple times within a single frame.
            const uint32_t slot = ActiveSlot();
            if ( PropertyDirty::ConsumeCleanThisFrame( m_LastCleanFrame[slot] ) && m_DirtyCount[slot] > 0 )
            {
                m_DirtyCount[slot]--;
            }
        }
        bool IsDirty() const
        {
            return m_DirtyCount[ActiveSlot()] > 0;
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
        // One counter per renderer slot — see MarkDirty.
        std::array<uint32_t, Engine::kMaxRendererSlots> m_DirtyCount{};
        std::array<uint64_t, Engine::kMaxRendererSlots> m_LastCleanFrame{};
    };
} // namespace Desert::Graphic
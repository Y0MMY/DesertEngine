#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>
#include <Common/Core/TemplateHelpers.hpp>

namespace Desert::Graphic
{
    class Material
    {
    public:
        explicit Material( std::string&& debugName, std::string&& shaderName )
             : m_MaterialExecutor( std::move(
                    Graphic::MaterialExecutor::Create( std::move( debugName ), std::move( shaderName ) ) ) )
        {
        }

        virtual ~Material() = default;

        virtual const MaterialExecutor* GetMaterialExecutor() const final
        {
            return m_MaterialExecutor.get();
        }

        template <typename T>
        T* Get( const std::string& name ) const
        {
            if constexpr ( std::is_same_v<T, UniformBufferProperty> )
            {
                return m_MaterialExecutor->GetUniformBufferProperty( name ).get();
            }

            else if constexpr ( std::is_same_v<T, StorageBufferProperty> )
            {
                return m_MaterialExecutor->GetStorageBufferProperty( name ).get();
            }

            else if constexpr ( std::is_same_v<T, Texture2DProperty> )
            {
                return m_MaterialExecutor->GetTexture2DProperty( name ).get();
            }

            else if constexpr ( std::is_same_v<T, TextureCubeProperty> )
            {
                return m_MaterialExecutor->GetTextureCubeProperty( name ).get();
            }

            DESERT_VERIFY( false, "Unsupported MaterialProperty type" );
            return nullptr;
        }

    protected:
        std::unique_ptr<MaterialExecutor> m_MaterialExecutor;
        bool                              m_ParametersDirty = false;
    };
} // namespace Desert::Graphic
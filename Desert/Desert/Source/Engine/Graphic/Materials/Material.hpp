#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>
#include <Common/Core/TemplateHelpers.hpp>

#include "MaterialInstance.hpp"

namespace Desert::Graphic
{
    class Material
    {
    public:
        explicit Material( std::string&& debugName, std::string&& shaderName );

        virtual ~Material() = default;

        MaterialInstancePtr CreateInstance( const std::string& name = "" );

        virtual const MaterialExecutor* GetMaterialExecutor() const final
        {
            return m_MaterialExecutor.get();
        }

        void SetDefaultParameter( const std::string& name, const MaterialPropertyValue& value,
                                  MaterialPropertyType type );
        const MaterialPropertySet& GetDefaultProperties() const
        {
            return m_DefaultProperties;
        }

        virtual void Bind( const MaterialInstance* instance );

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

        const std::vector<std::string>& GetPropertyNames() const
        {
            return m_PropertyNames;
        }

    private:
        // Helper methods for setting properties on executor
        void SetFloat( const std::string& propertyName, float value );
        void SetInt( const std::string& propertyName, int value );
        void SetVec3( const std::string& propertyName, const glm::vec3& value );
        void SetVec4( const std::string& propertyName, const glm::vec4& value );
        void SetMat4( const std::string& propertyName, const glm::mat4& value );
        void SetTexture( const std::string& propertyName, Texture2D* texture );
        void SetTexture( const std::string& propertyName, TextureCube* texture );

    protected:
        virtual void OnBind( MaterialInstance* instance )
        {
        }
        void CachePropertyNames();

        void ApplyPropertyToExecutor( const std::string&                           name,
                                      const MaterialPropertySet::MaterialProperty& property );

        MaterialPropertySet               m_DefaultProperties;
        std::vector<std::string>          m_PropertyNames;
        std::unique_ptr<MaterialExecutor> m_MaterialExecutor;
    };
} // namespace Desert::Graphic
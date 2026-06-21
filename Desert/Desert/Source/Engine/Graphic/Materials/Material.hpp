#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>
#include <Engine/Graphic/Materials/Properties/FieldProperty.hpp>
#include <Engine/Graphic/Materials/Properties/TProperty.hpp>
#include <Common/Core/TemplateHelpers.hpp>

#include "MaterialInstance.hpp"

#include <unordered_set>

namespace Desert::Graphic
{
    class Material : public IPropertyOwner
    {
    public:
        explicit Material( std::string&& debugName, std::string&& shaderName );

        virtual ~Material() = default;

        MaterialInstancePtr CreateInstance( const std::string& name = "" );

        virtual const MaterialExecutor* GetMaterialExecutor() const final
        {
            return m_MaterialExecutor.get();
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

    protected:
        // Uploads all dirty TProperty members to the matching FieldProperty/Texture slot.
        // Exposed as protected so non-instance Bind() overrides (JFA, etc.) can flush manually.
        void UploadRegisteredProperties( std::unordered_set<UniformBufferProperty*>& outDirtyUBs );

        // Searches all UniformBufferProperties in the executor for a field named fieldName.
        std::pair<UniformBufferProperty*, FieldProperty*> FindFieldInAnyUB( std::string_view fieldName ) const;

    private:
        // Applies MaterialInstance overrides on top of TProperty defaults.
        void ApplyInstanceOverrides( const MaterialInstance*                    instance,
                                     std::unordered_set<UniformBufferProperty*>& outDirtyUBs );

    protected:
        void RegisterProperty( IProperty* prop ) override;

        const std::vector<IProperty*>& GetRegisteredProperties() const
        {
            return m_RegisteredProperties;
        }

        virtual void OnBind( MaterialInstance* instance )
        {
        }
        void CachePropertyNames();

        std::vector<std::string>          m_PropertyNames;
        std::unique_ptr<MaterialExecutor> m_MaterialExecutor;
        std::vector<IProperty*>           m_RegisteredProperties;
    };
} // namespace Desert::Graphic
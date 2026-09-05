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

        // Name the row of the shared `Materials[]` storage buffer that the NEXT recorded draw reads.
        //
        // ON `Material` AND NOT ON ITS SUBCLASSES BECAUSE THERE IS ONE TRANSPORT. A PBR surface, a shader
        // graph, the terrain and the SDF text all deliver their parameters as a row indexed by a push
        // constant at Core::Formats::kMaterialIndexPushOffset — so this writes that one offset for all of
        // them, and there is no second place a second offset could be written. (There used to be a second
        // transport, a uniform block per material, and it could not give two objects different values at
        // all; MaterialParamRow.hpp records what that cost and how it was measured.)
        //
        // Written STRAIGHT INTO THE PUSH BUFFER rather than stored for a later Bind: a push constant is
        // snapshotted by Vulkan when the draw is recorded, so this is already per-draw state and holding
        // it on the material would be one more thing the next object could clobber. Generic draws never
        // call Bind at all — they submit an executor — so a stored value would never be pushed for them.
        void SetMaterialIndex( uint32_t index );

        // Public for editor introspection (PropertyEditorBuilder reads reflected properties to build UI).
        const std::vector<IProperty*>& GetRegisteredProperties() const
        {
            return m_RegisteredProperties;
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

        const std::vector<std::string>& GetPropertyNames() const
        {
            return m_PropertyNames;
        }

    protected:
        // Uploads all dirty TProperty members to the matching FieldProperty/Texture slot.
        // Exposed as protected so non-instance Bind() overrides (JFA, etc.) can flush manually.
        //
        // It used to hand back the set of uniform buffers it had touched, and all four callers threw
        // that set away -- an out-parameter nobody read, which is a dead parameter by any reading of
        // the contract. It is gone rather than plumbed: the flush below deliberately does NOT use "the
        // buffers touched this frame" (TProperty::Set skips unchanged values, so a constant parameter
        // would stop being reported and its other frame copies would go stale -- that was the outline
        // flicker), so the set had no possible consumer.
        void UploadRegisteredProperties();

        // Push every FIELD-FILLED uniform buffer that still owes a copy into that copy. The one
        // implementation of a loop that existed in four copies (here, tonemap, JFA composite, deferred
        // lighting) and had to be right about which buffers were safe to flush in each of them.
        //
        // Whole-filled buffers are skipped, and not by a list: they answer false to HasDirtyFields()
        // because their field shadow copies are not a source of truth. See
        // ShaderResources::BufferFillKind.hpp for what flushing one of them did to a frame.
        void FlushFieldFilledUniformBuffers();

        // Searches all UniformBufferProperties in the executor for a field named fieldName.
        std::pair<UniformBufferProperty*, FieldProperty*> FindFieldInAnyUB( std::string_view fieldName ) const;

    private:
        // Applies MaterialInstance overrides on top of TProperty defaults.
        void ApplyInstanceOverrides( const MaterialInstance* instance );

    protected:
        void RegisterProperty( IProperty* prop ) override;

        virtual void OnBind( MaterialInstance* instance )
        {
        }
        void CachePropertyNames();

        std::vector<std::string>          m_PropertyNames;
        std::unique_ptr<MaterialExecutor> m_MaterialExecutor;
        std::vector<IProperty*>           m_RegisteredProperties;
    };
} // namespace Desert::Graphic
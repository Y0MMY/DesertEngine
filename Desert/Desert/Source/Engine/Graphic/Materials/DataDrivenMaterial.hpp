#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Core/Formats/ShaderProgramMeta.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Graphic/Shader.hpp>

#include <glm/glm.hpp>
#include <unordered_set>

namespace Desert::Graphic
{
    class Image2D;

    // Generic, data-driven material built from ANY shader by name — no per-shader C++ class. Parameters
    // come from the shader's reflected uniform-buffer fields (offsets/types) plus the `#pragma param`
    // schema (UI metadata + defaults). Setting a param by name routes to the matching UB field via the
    // base Material's reflection. This is what makes arbitrary shaders assignable with dynamic params.
    class DataDrivenMaterial : public Material
    {
    public:
        explicit DataDrivenMaterial( const std::string& shaderName )
             : Material( "DDM_" + shaderName, std::string( shaderName ) ), m_ShaderName( shaderName )
        {
            if ( auto shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( shaderName ) )
                m_Schema = shader->GetProgramMeta();

            ApplyDefaults();
        }

        const Core::Formats::ShaderProgramMeta& GetSchema() const
        {
            return m_Schema;
        }
        const std::string& GetShaderName() const
        {
            return m_ShaderName;
        }

        // Number of schema params that mapped to an actual UB field (vs declared-but-absent). Useful to
        // verify the reflection<->annotation wiring.
        uint32_t GetMappedParamCount() const
        {
            return m_MappedParamCount;
        }

        // Write a scalar/vector param by name into whatever UB field it maps to. numComponents = 1..4.
        bool SetParam( const std::string& name, const glm::vec4& value, uint32_t numComponents )
        {
            auto [ub, field] = FindFieldInAnyUB( name );
            if ( !field )
                return false;
            field->SetRawBytes( &value, numComponents * sizeof( float ) );
            return true;
        }

        // Write a param by name using the UB field's own size (no component count needed). Used to apply
        // MaterialComponent overrides generically. UpdateFields() pushes the field into the UB buffer AND
        // marks it dirty so the executor's Apply() actually uploads it (a bare SetRawBytes does not).
        bool SetParamRaw( const std::string& name, const glm::vec4& value )
        {
            auto [ub, field] = FindFieldInAnyUB( name );
            if ( !field )
                return false;
            size_t sz = field->GetFieldInfo().Size;
            if ( sz > sizeof( glm::vec4 ) )
                sz = sizeof( glm::vec4 );
            field->SetRawBytes( &value, sz );
            if ( ub )
                ub->UpdateFields();
            return true;
        }

        // Bind a texture by its sampler name (the #pragma param texture2D name). Unset samplers keep the
        // backend's fallback texture, so a shader with an unassigned texture still renders.
        bool SetTexture( const std::string& name, const Image2D* image )
        {
            if ( !image )
                return false;
            if ( auto* tex = Get<Texture2DProperty>( name ) )
            {
                tex->SetImage( image );
                return true;
            }
            return false;
        }

        // Seed every numeric param with its `#pragma param ... default(...)` value, then flush the touched
        // UBs so the defaults reach the GPU (see SetParamRaw note on UpdateFields()).
        void ApplyDefaults()
        {
            m_MappedParamCount = 0;
            m_ParamBuffers.clear();
            for ( const auto& p : m_Schema.Params )
            {
                if ( p.IsTexture )
                    continue;
                auto [ub, field] = FindFieldInAnyUB( p.Name );
                if ( !field )
                    continue;

                ++m_MappedParamCount;
                size_t sz = field->GetFieldInfo().Size;
                if ( sz > sizeof( glm::vec4 ) )
                    sz = sizeof( glm::vec4 ); // params are scalars/vectors (<= 16 bytes)
                field->SetRawBytes( &p.Default, sz );
                if ( ub )
                    m_ParamBuffers.insert( ub );
            }
            for ( auto* ub : m_ParamBuffers )
                ub->UpdateFields();
        }

        // Re-push the parameter values this material already holds into the uniform-buffer copy the
        // CURRENT (frame x renderer slot) will read. Every generic draw calls this.
        //
        // UpdateFields() writes exactly ONE copy — whichever pair is recording (see
        // ShaderResources::BufferCopyIndex). The per-slot dirty counter is standing permission to keep
        // writing until every copy has been served, but permission is not the write: somebody has to come
        // back on the following frames and perform it. Materials bound through a MaterialInstance get that
        // from Material::Bind. Generic draws submit an executor and never Bind, so a per-slot material —
        // whose parameters are applied once, when its asset loads — reached a single frame-in-flight copy
        // and the mesh rendered BLACK on the other two, the unwritten copies being zero-filled.
        //
        // ONLY the buffers that carry schema parameters, and that restriction is load-bearing. The
        // engine-filled blocks (CameraUB, TimeUB, DirectionLightsUB) are written whole by
        // UniformBufferProperty::SetRawData, which does not go through FieldProperty at all — their field
        // local data is never initialised. Flushing those would memcpy uninitialised bytes over the camera
        // matrices the renderer had just written, which empties the frame. Measured, not imagined: the
        // first version of this fix flushed every buffer and the probe scene rendered as bare sky.
        void FlushParameterBuffers()
        {
            for ( auto* ub : m_ParamBuffers )
                if ( ub->HasDirtyFields() )
                    ub->UpdateFields();
        }

    private:
        std::string                      m_ShaderName;
        Core::Formats::ShaderProgramMeta m_Schema;
        uint32_t                         m_MappedParamCount = 0;
        // The UBs that schema parameters map to — the only ones whose FieldProperty local data is a
        // source of truth. Recomputed by ApplyDefaults; stable for the life of the material.
        std::unordered_set<UniformBufferProperty*> m_ParamBuffers;
    };
} // namespace Desert::Graphic

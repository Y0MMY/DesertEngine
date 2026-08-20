#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/Properties/StorageBufferProperty.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>

#include <Engine/Graphic/Clouds/CloudShadowPayload.hpp>

#include <Engine/Graphic/ShaderProtocols/PointLight.hpp>
#include <Engine/Graphic/ShaderProtocols/SpotLight.hpp>
#include <Engine/Graphic/ShaderProtocols/Metadata.hpp>

#include <glm/glm.hpp>

#include <unordered_set>

namespace Desert::Graphic
{
    // CSM data the deferred lighting pass needs to shadow the sun (mirrors MaterialPBRBase::UpdateShadow).
    struct DeferredShadowInput
    {
        const glm::mat4* CascadeVP            = nullptr; // [Count] light view-proj matrices
        Image2D*         CascadeMaps[4]       = { nullptr, nullptr, nullptr, nullptr };
        uint32_t         Count                = 0;
        float            Bias                 = 0.005f;
        bool             Enabled              = true;
        glm::vec4        CascadeWorldPerTexel = glm::vec4( 1.0f );
    };

    // THE CLOUD LAYER'S SHADOW ON THE WORLD — a SECOND, independent occluder of the same sun, and a
    // separate struct from DeferredShadowInput on purpose. The cascades are a depth comparison against
    // opaque geometry; this is a volumetric transmittance reconstructed from one texel of one map. They
    // share nothing but the light they attenuate, and folding them into one block would have put a cloud
    // parameter inside the layout PBR.glsl.frag mirrors for its cascades.
    //
    // `Map` null, or `Enabled` false, is the ordinary state: no cloud component, clouds off, casting off,
    // strength zero, or a renderer whose scene has no sky at all. The material then leaves the sampler on
    // its dummy image and the shader is told not to read it.
    struct CloudShadowInput
    {
        Image2D*  Map = nullptr;
        glm::mat4 WorldToMap{ 1.0f };
        float     FarDepthKm   = 0.0f;
        float     Strength     = 0.0f;
        float     BorderFadeUv = kCloudShadowBorderFadeUv;
        bool      Enabled      = false;
    };

    // Fullscreen deferred-lighting material: binds the scene renderer's G-buffer color targets (albedo/metallic,
    // normal/roughness, world-position) + the sun (+ its CSM shadow maps) + ALL point & spot lights (uploaded
    // into the shared SSBO layout the mesh PBR shader also uses) + a debug-mode selector, driving
    // DeferredLighting.shader. Header-only (no new .cpp -> no premake regen).
    class MaterialDeferredLighting final : public Material
    {
    public:
        MaterialDeferredLighting() : Material( "MaterialDeferredLighting", "DeferredLighting" )
        {
            m_GBufferA        = m_MaterialExecutor->GetTexture2DProperty( "u_GBufferA" ).get();
            m_GBufferB        = m_MaterialExecutor->GetTexture2DProperty( "u_GBufferB" ).get();
            m_GBufferC        = m_MaterialExecutor->GetTexture2DProperty( "u_GBufferC" ).get();
            m_GBufferEmissive = m_MaterialExecutor->GetTexture2DProperty( "u_GBufferEmissive" ).get();
            m_SSAO            = m_MaterialExecutor->GetTexture2DProperty( "u_SSAO" ).get();
            m_GI              = m_MaterialExecutor->GetTexture2DProperty( "u_GI" ).get();
            m_CloudShadowMap  = m_MaterialExecutor->GetTexture2DProperty( "u_CloudShadowMap" ).get();
        }

        // gA = Albedo+Metallic, gB = Normal+Roughness, gC = WorldPosition; lightDir.xyz = direction the sun
        // travels; lightColor.rgb/.a = colour/intensity; cameraPos.xyz = camera world pos (view vector);
        // debugMode 0=Lit,1=Albedo,2=Normal,3=Metallic,4=Roughness; point/spot = the scene's dynamic lights.
        void Bind( const std::shared_ptr<Image2D>& gA, const std::shared_ptr<Image2D>& gB,
                   const std::shared_ptr<Image2D>& gC, const std::shared_ptr<Image2D>& gE,
                   const glm::vec4& lightDir, const glm::vec4& lightColor, const glm::vec4& cameraPos,
                   int debugMode, const ShaderProtocols::PointLight& pointLights,
                   const ShaderProtocols::SpotLight& spotLights, const DeferredShadowInput& shadow,
                   const std::shared_ptr<Image2D>& aoImage, float giIntensity, bool ssaoEnabled, int giMode,
                   const std::shared_ptr<Image2D>& giImage, const CloudShadowInput& cloudShadow )
        {
            if ( m_GBufferA && gA )
                m_GBufferA->SetImage( gA.get() );
            if ( m_GBufferB && gB )
                m_GBufferB->SetImage( gB.get() );
            if ( m_GBufferC && gC )
                m_GBufferC->SetImage( gC.get() );
            if ( m_GBufferEmissive && gE )
                m_GBufferEmissive->SetImage( gE.get() );
            if ( m_SSAO && aoImage )
                m_SSAO->SetImage( aoImage.get() );
            // RSM mode only: the resolved indirect-light buffer. In the screen-space / off modes nothing is
            // bound here and the shader never samples it (the descriptor keeps its dummy image).
            if ( m_GI && giImage )
                m_GI->SetImage( giImage.get() );

            SetLightDir( lightDir );
            SetLightColor( lightColor );
            SetCameraPos( cameraPos );
            // u_Params: x = debug mode, y = GI intensity (0 = off), z = SSAO enabled (else shader uses AO=1),
            // w = GI mode (0 = off, 1 = screen-space gather, 2 = RSM buffer). Mode picks WHERE the indirect
            // light comes from; intensity scales it (the RSM path pre-applies it in GIResolve).
            SetParams( glm::vec4( static_cast<float>( debugMode ), giIntensity, ssaoEnabled ? 1.0f : 0.0f,
                                  static_cast<float>( giMode ) ) );

            UploadShadow( shadow );
            UploadCloudShadow( cloudShadow );

            // Upload the dynamic lights into the same SSBO/UB layout the mesh PBR shader uses (bindings 6/16/4).
            // Empty is fine — the shader loops 0..count, so an untouched buffer is simply never read; but the
            // metadata counts must always be current.
            if ( !pointLights.PointLights.empty() )
            {
                if ( auto* sb = Get<StorageBufferProperty>( ShaderProtocols::PointLight::Name ) )
                    sb->SetRawData( (std::byte*)pointLights.PointLights.data(),
                                    static_cast<uint32_t>( pointLights.PointLights.size() *
                                                           sizeof( ShaderProtocols::PointLightPayload ) ) );
            }
            if ( !spotLights.SpotLights.empty() )
            {
                if ( auto* sb = Get<StorageBufferProperty>( ShaderProtocols::SpotLight::Name ) )
                    sb->SetRawData( (std::byte*)spotLights.SpotLights.data(),
                                    static_cast<uint32_t>( spotLights.SpotLights.size() *
                                                           sizeof( ShaderProtocols::SpotLightPayload ) ) );
            }

            const uint32_t counts[3] = { 0u, static_cast<uint32_t>( pointLights.PointLights.size() ),
                                         static_cast<uint32_t>( spotLights.SpotLights.size() ) };
            if ( auto* meta = Get<UniformBufferProperty>( ShaderProtocols::LightsMetadata::Name ) )
                meta->SetRawData( (std::byte*)counts, sizeof( counts ) );

            std::unordered_set<UniformBufferProperty*> dirty;
            UploadRegisteredProperties( dirty );
            for ( const auto& [ubName, idx] : m_MaterialExecutor->GetUniformBufferProperties() )
            {
                auto ub = m_MaterialExecutor->GetUniformBufferProperty( ubName );
                if ( ub && ub->HasDirtyFields() )
                    ub->UpdateFields();
            }
        }

        // Uploads the CSM data into ShadowUB + binds the cascade maps (u_ShadowMap0..3) — mirrors
        // MaterialPBRBase::UpdateShadow so the SAME sun shadows appear in the deferred path.
        void UploadShadow( const DeferredShadowInput& shadow )
        {
            struct ShadowUBData
            {
                glm::mat4 LightViewProj[4];
                glm::vec4 Params;            // x = bias, y = enabled, z = debug mode, w = cascade count
                glm::vec4 DebugParams;
                glm::vec4 CascadeTexelWorld;
            } data;

            const uint32_t n = shadow.Count < 4u ? shadow.Count : 4u;
            for ( uint32_t i = 0; i < 4u; ++i )
                data.LightViewProj[i] = ( i < n && shadow.CascadeVP ) ? shadow.CascadeVP[i] : glm::mat4( 1.0f );
            data.Params = glm::vec4( shadow.Bias, shadow.Enabled ? 1.0f : 0.0f, 0.0f, static_cast<float>( n ) );
            data.DebugParams       = glm::vec4( 0.0f );
            data.CascadeTexelWorld = shadow.CascadeWorldPerTexel;

            if ( auto* ub = Get<UniformBufferProperty>( "ShadowUB" ) )
                ub->SetRawData( reinterpret_cast<const std::byte*>( &data ), sizeof( data ) );

            static const char* kNames[4] = { "u_ShadowMap0", "u_ShadowMap1", "u_ShadowMap2", "u_ShadowMap3" };
            for ( uint32_t i = 0; i < 4u; ++i )
            {
                Image2D* img = ( i < n ) ? shadow.CascadeMaps[i] : nullptr;
                if ( img )
                    if ( auto* tex = Get<Texture2DProperty>( kNames[i] ) )
                        tex->SetImage( img );
            }
        }

        // Uploads the cloud layer's shadow into CloudShadowUB + binds the map. Nothing is bound when the
        // layer is not casting: the sampler keeps its dummy image and `Params.y` is 0, which is the one
        // number the shader tests before it fetches — so a scene with no clouds costs one uniform upload
        // of eighty bytes and not a texture read per pixel.
        void UploadCloudShadow( const CloudShadowInput& cloudShadow )
        {
            const bool live = cloudShadow.Enabled && cloudShadow.Map != nullptr && cloudShadow.Strength > 0.0f;

            CloudShadowUniforms data;
            data.WorldToMap = live ? cloudShadow.WorldToMap : glm::mat4( 1.0f );
            data.Params     = glm::vec4( cloudShadow.FarDepthKm, live ? 1.0f : 0.0f, cloudShadow.BorderFadeUv,
                                         cloudShadow.Strength );

            if ( auto* ub = Get<UniformBufferProperty>( "CloudShadowUB" ) )
                ub->SetRawData( reinterpret_cast<const std::byte*>( &data ), sizeof( data ) );

            if ( live && m_CloudShadowMap )
                m_CloudShadowMap->SetImage( cloudShadow.Map );
        }

        MPROPERTY( glm::vec4, LightDir,   "u_LightDir",   ( glm::vec4( 0.0f, -1.0f, 0.0f, 0.0f ) ) )
        MPROPERTY( glm::vec4, LightColor, "u_LightColor", ( glm::vec4( 1.0f, 1.0f, 1.0f, 3.0f ) ) )
        MPROPERTY( glm::vec4, Params,     "u_Params",     ( glm::vec4( 0.0f ) ) )
        MPROPERTY( glm::vec4, CameraPos,  "u_CameraPos",  ( glm::vec4( 0.0f ) ) )

    private:
        Texture2DProperty* m_GBufferA        = nullptr;
        Texture2DProperty* m_GBufferB        = nullptr;
        Texture2DProperty* m_GBufferC        = nullptr;
        Texture2DProperty* m_GBufferEmissive = nullptr;
        Texture2DProperty* m_SSAO            = nullptr;
        Texture2DProperty* m_GI              = nullptr;
        Texture2DProperty* m_CloudShadowMap  = nullptr;
    };
} // namespace Desert::Graphic

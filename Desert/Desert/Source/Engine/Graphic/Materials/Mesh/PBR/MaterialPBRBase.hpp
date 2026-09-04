#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Assets/MaterialAsset.hpp>

#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/Clouds/CloudShadowPayload.hpp>
#include <Engine/Graphic/ShaderProtocols/PointLight.hpp>
#include <Engine/Graphic/ShaderProtocols/SpotLight.hpp>
#include <Engine/Graphic/ShaderProtocols/DirectionLight.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    class Image2D;
    class ImageCube;

    // Shared base for PBR materials: provides the per-frame scene-data uploads (camera + lights) into
    // the shared executor uniform buffers. Material parameters themselves live in the reflected
    // Assets::PBRSurfaceParams and travel via push constants (see PBRPush.hpp), not through this base.
    class MaterialPBRBase : public Material
    {
    public:
        // How many shadow cascades the ShadowUB block below carries, and therefore how many cascade maps
        // a lit draw binds. ONE number for the whole chain: this mirror, PBRSceneFrame::CascadeMaps and
        // MeshRenderer::kNumCascades are all defined from it, so a fifth cascade cannot be added to one
        // of them and forgotten in the other two.
        static constexpr uint32_t kMaxCascades = 4;

        // The C++ half of the `ShadowUB` block every mesh PBR shader declares. It is PUBLIC because it
        // is one half of a pair that must agree byte for byte, and the other half is GLSL: reflecting
        // the block and comparing it with this is the only way to assert that agreement on a machine
        // with no device (Desert/Tests/Engine/PBRSceneFrame).
        struct ShadowUBData
        {
            glm::mat4 LightViewProj[kMaxCascades];
            glm::vec4 Params;            // x = bias, y = enabled, z = debug mode, w = cascade count
            glm::vec4 DebugParams;       // x = show normals, y = lighting debug
            glm::vec4 CascadeTexelWorld; // per-cascade world size of one shadow-map texel
        };

        // THE names the writers below look up. A material binds by NAME (Material::Get), so these — and
        // not the slot numbers, which differ between the static and the skinned shader — are what the
        // shaders and the CPU have to agree on. Named constants rather than literals at the call site so
        // the test can assert the agreement against the same strings the writer uses.
        static constexpr const char* kShadowBlockName              = "ShadowUB";
        static constexpr const char* kShadowMapNames[kMaxCascades] = { "u_ShadowMap0", "u_ShadowMap1",
                                                                       "u_ShadowMap2", "u_ShadowMap3" };
        static constexpr const char* kEnvIrradianceName            = "u_EnvIrradianceTex";
        static constexpr const char* kEnvSpecularName              = "u_EnvSpecularTex";
        static constexpr const char* kBrdfLutName                  = "u_BRDFLUTTexture";

        static void UpdateCamera( MaterialInstance* instance, const Core::Camera* camera );
        static void UpdateLights( MaterialInstance* instance, const ShaderProtocols::PointLight& pointLights,
                                  const ShaderProtocols::SpotLight&      spotLights,
                                  const ShaderProtocols::DirectionLight& dirLights );
        // Binds the cascaded directional shadow maps + per-cascade light view-projections (and bias /
        // enable / debug mode) for the PBR pass. cascadeViewProj/cascadeMaps have numCascades entries.
        static void UpdateShadow( MaterialInstance* instance, const glm::mat4* cascadeViewProj,
                                  Image2D* const* cascadeMaps, uint32_t numCascades, float bias, bool enabled,
                                  int debugMode, bool showNormals, const glm::vec4& cascadeWorldPerTexel,
                                  bool lightingDebug = false );
        // Binds the IBL inputs for the PBR pass: diffuse irradiance + prefiltered specular cubemaps and
        // the (precomputed) split-sum BRDF LUT.
        static void UpdateEnvironment( MaterialInstance* instance, ImageCube* irradiance, ImageCube* prefiltered,
                                       Image2D* brdfLut );
        // Binds the cloud layer's shadow — the sun's SECOND occluder, beside the cascades above. The
        // payload is the one SceneRenderer gathered for the whole frame, and it is written through the
        // one shared writer (Graphic::CloudShadowBind), so a forward-shaded surface and a deferred-shaded
        // one cannot be told different things about the same map. Until Р21 this call did not exist and
        // the deferred composite was the only consumer in the engine.
        static void UpdateCloudShadow( MaterialInstance* instance, const CloudShadowInput& cloudShadow );

    protected:
        MaterialPBRBase( std::string&& debugName, std::string&& shaderName );
        ~MaterialPBRBase() override = default;

        static void UpdatePointLights( MaterialInstance* instance, const ShaderProtocols::PointLight& lights );
        static void UpdateSpotLights( MaterialInstance* instance, const ShaderProtocols::SpotLight& lights );
        static void UpdateDirectionLights( MaterialInstance* instance, const ShaderProtocols::DirectionLight& lights );
        static void UpdateLightsMetadata( MaterialInstance* instance, const ShaderProtocols::PointLight& point,
                                          const ShaderProtocols::SpotLight&      spot,
                                          const ShaderProtocols::DirectionLight& dir );
    };
} // namespace Desert::Graphic

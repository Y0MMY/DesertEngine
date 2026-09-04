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

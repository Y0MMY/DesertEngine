#include "MaterialPBRBase.hpp"

#include <Engine/Assets/Mapper.hpp>

#include <Engine/Graphic/Clouds/CloudShadowBinding.hpp>
#include <Engine/Graphic/Materials/SceneLightingBinding.hpp>
#include <Engine/Graphic/Image.hpp>

namespace Desert::Graphic
{
    // Every function here is now a two-line adapter: MaterialInstance -> Material, then the ONE writer of
    // that block (Engine/Graphic/Materials/SceneLightingBinding.hpp, and CloudShadowBinding.hpp for the
    // cloud pair). The bodies moved rather than being copied, because a MaterialInstance is exactly what
    // the generic mesh path does NOT have: a data-driven material draws through a Material and an
    // executor, so while these signatures were the only way to reach the blocks, the generic path could
    // not receive them and grew a partial filler of its own instead. Keeping the adapters is what leaves
    // the PBR call sites untouched.

    MaterialPBRBase::MaterialPBRBase( std::string&& debugName, std::string&& shaderName )
         : Material( std::move( debugName ), std::move( shaderName ) )
    {
    }

    void MaterialPBRBase::UpdateCamera( MaterialInstance* instance, const Core::Camera* camera )
    {
        if ( !instance )
            return;
        SceneCameraBind( instance->GetParentMaterial(), camera );
    }

    void MaterialPBRBase::UpdateLights( MaterialInstance* instance, const ShaderProtocols::PointLight& pointLights,
                                        const ShaderProtocols::SpotLight&      spotLights,
                                        const ShaderProtocols::DirectionLight& dirLights )
    {
        if ( !instance )
            return;
        SceneLightsBind( instance->GetParentMaterial(), pointLights, spotLights, dirLights );
    }

    void MaterialPBRBase::UpdateShadow( MaterialInstance* instance, const glm::mat4* cascadeViewProj,
                                        Image2D* const* cascadeMaps, uint32_t numCascades, float bias,
                                        bool enabled, int debugMode, bool showNormals,
                                        const glm::vec4& cascadeWorldPerTexel, bool lightingDebug )
    {
        if ( !instance )
            return;
        SceneShadowBind( instance->GetParentMaterial(), cascadeViewProj, cascadeMaps, numCascades, bias, enabled,
                         debugMode, showNormals, cascadeWorldPerTexel, lightingDebug );
    }

    void MaterialPBRBase::UpdateEnvironment( MaterialInstance* instance, ImageCube* irradiance,
                                             ImageCube* prefiltered, Image2D* brdfLut )
    {
        if ( !instance )
            return;
        SceneEnvironmentBind( instance->GetParentMaterial(), irradiance, prefiltered, brdfLut );
    }

    void MaterialPBRBase::UpdateCloudShadow( MaterialInstance* instance, const CloudShadowInput& cloudShadow )
    {
        if ( !instance )
            return;
        CloudShadowBind( instance->GetParentMaterial(), cloudShadow );
    }

} // namespace Desert::Graphic

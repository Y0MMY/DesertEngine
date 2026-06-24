#pragma once

#include <glm/glm.hpp>

#include <Engine/Assets/Mesh/PBRMaterialData.hpp>

namespace Desert::Graphic
{
    // Per-object PBR material push-constant block. Layout must match the push_constant block in
    // Static.glsl.vert / PBR.glsl.frag (placed right after the mat4 Transform, at offset 64).
    struct PBRPushMaterial
    {
        glm::vec4 AlbedoAO;           // rgb = albedo, a = ambient occlusion
        glm::vec4 MetalRoughEmission; // x = metallic, y = roughness, z = emission strength
        glm::vec4 EmissionColor;      // rgb = emission color
    };

    // Packs the reflected material data into the push block — the single mapping data -> shader.
    inline PBRPushMaterial BuildPBRPushMaterial( const Assets::PBRMaterialData& d )
    {
        return { glm::vec4( glm::vec3( d.AlbedoColor ), d.AOStrength ),
                 glm::vec4( d.MetallicFactor, d.RoughnessFactor, d.EmissiveIntensity, 0.0f ),
                 glm::vec4( glm::vec3( d.EmissiveColor ), 0.0f ) };
    }
} // namespace Desert::Graphic

#pragma once

#include <glm/glm.hpp>

#include <Engine/Assets/Common.hpp>          // Desert::Assets::AssetHandle
#include <Engine/Reflection/ReflectionMacros.hpp>

namespace Desert::Assets
{
    // Canonical, reflected PBR material parameters. Editor UI, .lmat (de)serialization and shader
    // upload are all derived from these PROPERTY annotations via DesertHeaderTool — there is no
    // hand-written per-parameter code. Keep this a plain standard-layout aggregate (offsetof-safe).
    struct PBRMaterialData
    {
        REFLECT()

        PROPERTY( DisplayName( "Albedo" ), Category( "Surface" ), Color )
        glm::vec4 AlbedoColor = glm::vec4( 1.0f );

        PROPERTY( DisplayName( "Metallic" ), Category( "Surface" ), Range( 0.0f, 1.0f ) )
        float MetallicFactor = 0.0f;

        PROPERTY( DisplayName( "Roughness" ), Category( "Surface" ), Range( 0.0f, 1.0f ) )
        float RoughnessFactor = 0.5f;

        PROPERTY( DisplayName( "Ambient Occlusion" ), Category( "Surface" ), Range( 0.0f, 1.0f ) )
        float AOStrength = 1.0f;

        PROPERTY( DisplayName( "Emissive" ), Category( "Surface" ), Color )
        glm::vec4 EmissiveColor = glm::vec4( 0.0f, 0.0f, 0.0f, 1.0f );

        PROPERTY( DisplayName( "Emissive Intensity" ), Category( "Surface" ), Range( 0.0f, 100.0f ) )
        float EmissiveIntensity = 1.0f;

        PROPERTY( DisplayName( "Albedo Map" ), Category( "Textures" ), Asset<TextureAsset>, Thumbnail )
        AssetHandle AlbedoTexture{ 0ULL };

        PROPERTY( DisplayName( "Normal Map" ), Category( "Textures" ), Asset<TextureAsset>, Thumbnail )
        AssetHandle NormalTexture{ 0ULL };

        PROPERTY( DisplayName( "Metallic Map" ), Category( "Textures" ), Asset<TextureAsset>, Thumbnail )
        AssetHandle MetallicTexture{ 0ULL };

        PROPERTY( DisplayName( "Roughness Map" ), Category( "Textures" ), Asset<TextureAsset>, Thumbnail )
        AssetHandle RoughnessTexture{ 0ULL };

        PROPERTY( DisplayName( "AO Map" ), Category( "Textures" ), Asset<TextureAsset>, Thumbnail )
        AssetHandle AOTexture{ 0ULL };

        PROPERTY( DisplayName( "Emissive Map" ), Category( "Textures" ), Asset<TextureAsset>, Thumbnail )
        AssetHandle EmissiveTexture{ 0ULL };
    };
} // namespace Desert::Assets

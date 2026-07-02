#pragma once

#include <glm/glm.hpp>
#include <optional>

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

        // Alpha cutout (foliage/cards): 0 = disabled (opaque). When > 0, fragments whose Opacity Map value is
        // below this threshold are discarded — required for grass/leaves on alpha-mapped quads.
        PROPERTY( DisplayName( "Alpha Cutoff" ), Category( "Surface" ), Range( 0.0f, 1.0f ) )
        float AlphaCutoff = 0.0f;

        // --- Glass / transparency (UE-style) ---
        // Transmission > 0 makes the surface TRANSPARENT: it is skipped by the opaque (deferred G-buffer) path
        // and drawn instead in a forward transparent pass over the composited scene (alpha blend + Fresnel edge
        // + screen-space refraction of the scene behind it). 0 = opaque.
        PROPERTY( DisplayName( "Transmission" ), Category( "Glass" ), Range( 0.0f, 1.0f ) )
        float Transmission = 0.0f;

        // Index of refraction (glass ~1.5, water ~1.33) — drives how much the background bends behind the glass
        // and the strength of the grazing-angle Fresnel reflection.
        PROPERTY( DisplayName( "IOR" ), Category( "Glass" ), Range( 1.0f, 2.5f ) )
        float IOR = 1.5f;

        // Colour of the transmitted light (coloured/tinted glass). White = clear glass.
        PROPERTY( DisplayName( "Glass Tint" ), Category( "Glass" ), Color )
        glm::vec4 GlassTint = glm::vec4( 1.0f );

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

        PROPERTY( DisplayName( "Opacity Map" ), Category( "Textures" ), Asset<TextureAsset>, Thumbnail )
        AssetHandle OpacityTexture{ 0ULL };

        // Stable, persisted material identity = the "external" handle a mesh's submeshes reference to find
        // their default material (resolved via MaterialService::GetAssetHandleByExternal). NOT editor-exposed
        // (no PROPERTY). std::optional so .demat files written before this field existed still parse (absent
        // -> nullopt). Set deterministically at import / on editor-create so it survives re-cooks & renames.
        std::optional<Common::UUID> MaterialId;

        // UV tiling: the surface UVs are multiplied by this before sampling albedo/normal/opacity, so a
        // texture repeats N times across the surface (the "natygivanie/tiling" control — like UE). std::optional
        // so older .demat still parse (absent -> {1,1} = no tiling). Drawn via a manual widget in the Materials
        // panel (optional fields aren't auto-handled by the reflected PropertyEditor), uploaded to the GPU
        // material (PBRGpuMaterial::ExtraParams), and used in PBR.glsl.frag.
        std::optional<glm::vec2> UVTiling;
    };
} // namespace Desert::Assets

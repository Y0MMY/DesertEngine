#pragma once

#include <Engine/Graphic/FallbackTextures.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/Properties/Texture2DProperty.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    /**
     * @brief CLOUD SHADOWS ON THE WORLD, in transport form — what a lit surface needs to know about the
     *        deck standing between it and the sun.
     *
     * Filled once a frame by System::VolumetricCloudRenderer (which traced the map) and read by every
     * consumer that shades opaque geometry with the directional light: the deferred lighting pass, the
     * three forward PBR shaders, the terrain and the grass. It travels through SceneRenderer, which is
     * the only object all of those share.
     *
     * ITS THREE MEMBERS ARE THE MAP'S OWN FRAME, not the current one. Centre, SunDirection and Extent are
     * the values the pass was DISPATCHED with, carried alongside the image rather than re-derived by each
     * consumer from the camera and the light — a consumer that re-derived them would project through a
     * frame the map was not traced in, and every shadow on the ground would land somewhere other than
     * under the cloud that cast it. This is the same rule the cloud march's own shadow map lives under,
     * stated once here instead of five times out in the shaders.
     *
     * MAP == NULLPTR is the honest statement that no map exists this frame — no cloud layer, no light
     * asking for one, or the image could not be allocated. Strength is then 0 and every consumer's
     * shader returns exactly 1.0, bit for bit, which is what makes the feature's OFF state free of a
     * second code path.
     */
    struct CloudWorldShadowInput
    {
        // The RGBA16F sun-space map: r = front depth (km), g = mean extinction (/km), b = max optical
        // depth. Borrowed, never owned — VolumetricCloudRenderer owns it for the life of the view.
        Image2D* Map = nullptr;

        // xyz = the world point the map was traced around (the camera), w = its half-width in world units.
        glm::vec4 CentreExtent = glm::vec4( 0.0f );

        // xyz = TOWARD the sun, normalized, as the map was traced. w = the receiver-side strength, UE's
        // CloudShadowOnSurfaceStrength: 0 switches the whole thing off for this light.
        glm::vec4 SunStrength = glm::vec4( 0.0f, 1.0f, 0.0f, 0.0f );

        [[nodiscard]] bool Active() const
        {
            return Map != nullptr && SunStrength.w > 0.0f;
        }
    };

    /**
     * @brief The 32 bytes of `CloudWorldShadowUB`, byte for byte as the consumers declare it.
     *
     * Two vec4s and no padding: std140 gives a vec4 a 16-byte alignment and C++ (with glm's vec4) agrees,
     * so a block of exactly two of them has the same layout in both languages with nothing to assert
     * beyond its size.
     */
    struct CloudWorldShadowUBData
    {
        glm::vec4 CentreExtent;
        glm::vec4 SunStrength;
    };

    static_assert( sizeof( CloudWorldShadowUBData ) == 32 );

    /**
     * @brief Binds the world cloud-shadow map and its block on @p material.
     *
     * Called by every consumer's own Bind, from one place, because five shaders declaring the same two
     * names is five chances to fill them differently.
     *
     * A material whose shader declares neither name is left untouched — `Get` returns nullptr and this
     * costs two hash lookups — so it is safe to call unconditionally from a material shared by shaders
     * that do and do not read the map.
     *
     * The SAMPLER IS ALWAYS BOUND when the shader declares it, feature on or off: a declared descriptor
     * with nothing behind it is an invalid set, not an unused one. With no map this frame the engine's
     * 1x1 RGBA16F fallback stands in, and the strength of 0 in the block means the shader never fetches
     * it.
     */
    inline void CloudWorldShadowBind( Material& material, const CloudWorldShadowInput& input )
    {
        if ( auto* tex = material.Get<Texture2DProperty>( "u_CloudWorldShadowMap" ) )
        {
            Image2D* image = input.Map;
            if ( !image )
            {
                // RGBA32F and not RGBA16F, which is the map's own format: the engine builds fallbacks for
                // RGBA8F and RGBA32F only (VulkanFallbackTextures' constructor), and the format of a
                // combined image sampler that is never SAMPLED does not matter — the block's strength is
                // 0 whenever this stands in, and the shader's first line returns before the fetch.
                const auto& fallback =
                     FallbackTextures::Get().GetFallbackTexture2D( Core::Formats::ImageFormat::RGBA32F );
                image = fallback.get();
            }
            if ( image )
                tex->SetImage( image );
        }

        if ( auto* ub = material.Get<UniformBufferProperty>( "CloudWorldShadowUB" ) )
        {
            const CloudWorldShadowUBData data{
                 .CentreExtent = input.CentreExtent,
                 // A map that does not exist is a strength of zero, said HERE rather than trusted to the
                 // caller: the shader's whole OFF path hangs off this one number.
                 .SunStrength =
                      input.Map ? input.SunStrength
                                : glm::vec4( input.SunStrength.x, input.SunStrength.y, input.SunStrength.z, 0.0f ),
            };
            ub->SetRawData( reinterpret_cast<const std::byte*>( &data ), sizeof( data ) );
        }
    }
} // namespace Desert::Graphic

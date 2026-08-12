#pragma once

#include <Engine/ECS/SkyAtmosphereComponent.hpp>

#include <Common/Core/Units.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace Desert::Graphic
{
    // The DECISIONS the sky makes, as PURE MATH — no renderer, no GPU, no state.
    //
    // The editor cannot run in the build environment, so anything that lives inside SkyboxRenderer can only
    // be checked by looking at a picture. Everything here is a function of numbers instead, and every rule
    // below is pinned by Tests/Engine/SkyRules. The renderer's job is to fetch the arguments.

    // ---------------------------------------------------------------------------------------------------
    // Which sky the Sky pass draws
    // ---------------------------------------------------------------------------------------------------

    enum class SkyMode : uint8_t
    {
        None,       // nothing to draw; the previously baked environment is retained
        Atmosphere, // the procedural gradient sky (SkyAtmosphereComponent)
        HdrCubemap  // an HDR asset (SkyboxComponent)
    };

    // The old `Procedural` bool did double duty: "which pass" AND "is the sky on". Splitting the components
    // split that decision, so it has to be stated somewhere — otherwise both pipelines think they own the
    // frame and the one that runs last wins by accident.
    inline SkyMode ResolveSkyMode( bool hasEnabledAtmosphere, bool hasHdrSkybox )
    {
        if ( hasEnabledAtmosphere )
            return SkyMode::Atmosphere;
        if ( hasHdrSkybox )
            return SkyMode::HdrCubemap;
        return SkyMode::None;
    }

    // Which of several sky entities drives the frame. Entity ids, lowest wins.
    //
    // The collector used to take whichever entity `entt` visited first and `break`. That order is not
    // stable across a component add/remove, so a scene with two skies rendered a different one after an
    // unrelated edit — with no message anywhere. Lowest id is arbitrary but REPRODUCIBLE, which is the
    // property that matters, and the caller names every candidate in the log.
    inline std::optional<size_t> SelectPrimarySky( std::span<const uint64_t> skyEntityIds )
    {
        std::optional<size_t> best;
        for ( size_t i = 0; i < skyEntityIds.size(); ++i )
        {
            if ( !best || skyEntityIds[i] < skyEntityIds[*best] )
                best = i;
        }
        return best;
    }

    // ---------------------------------------------------------------------------------------------------
    // Time of day -> sun direction
    // ---------------------------------------------------------------------------------------------------

    // Where the sun is at @p hours of solar time, as the direction the sunlight TRAVELS (sun -> scene) —
    // which is exactly what a directional light's TransformComponent::Translation encodes.
    //
    // Horizon frame: +X east, +Y up, +Z north. Solar declination is ZERO: the model has no calendar, so
    // this is the equinox path — the sun rises due east, sets due west, and @p latitudeDeg only decides
    // how high noon gets. @p northOffsetDeg rotates solar north onto the scene's north about world +Y, in
    // DEGREES converted here (the reference implementation this model comes from fed 45 degrees into a
    // radians parameter and got a sun path nobody could explain).
    inline glm::vec3 SunDirectionFromTimeOfDay( float hours, float latitudeDeg, float northOffsetDeg )
    {
        // Hour angle: 0 at noon, -pi at midnight, +-pi/2 at 06:00 / 18:00.
        const float hourAngle = ( hours / 24.0f ) * glm::two_pi<float>() - glm::pi<float>();
        const float latitude  = glm::radians( latitudeDeg );

        const float sinH = std::sin( hourAngle );
        const float cosH = std::cos( hourAngle );

        // Already unit length: sin^2(H) + cos^2(H) * (cos^2(lat) + sin^2(lat)) == 1.
        const glm::vec3 towardSunSolar( -sinH, std::cos( latitude ) * cosH, -std::sin( latitude ) * cosH );

        const glm::vec3 towardSun = glm::vec3(
             glm::rotate( glm::mat4( 1.0f ), glm::radians( northOffsetDeg ), glm::vec3( 0.0f, 1.0f, 0.0f ) ) *
             glm::vec4( towardSunSolar, 0.0f ) );

        return -towardSun;
    }

    // Advances the clock by one frame. @p dayLengthSeconds == 0 freezes the sun at the authored hour, which
    // is what makes "Drive Sun From Time Of Day" usable as a posing tool and not only as an animation.
    inline float AdvanceTimeOfDay( float hours, float dtSeconds, float dayLengthSeconds )
    {
        if ( dayLengthSeconds <= 0.0f )
            return hours;

        float next = hours + dtSeconds * 24.0f / dayLengthSeconds;
        next       = std::fmod( next, 24.0f );
        if ( next < 0.0f )
            next += 24.0f;
        return next;
    }

    // ---------------------------------------------------------------------------------------------------
    // When the sky IBL is re-baked
    // ---------------------------------------------------------------------------------------------------

    // Baking idles the whole device and rebuilds four GPU images, so it cannot run per frame — but with the
    // time-of-day driver the sun moves EVERY frame, and the previous rule ("first enable or the button")
    // would have left the environment frozen at whatever hour the scene was opened at. The threshold is the
    // throttle, and it is a number a test can pin instead of a frame count nobody can reason about.
    //
    // @p bakedSunDir / @p currentSunDir are toward-sun directions; they need not be normalized.
    inline bool ShouldRebakeSkyEnvironment( const glm::vec3& bakedSunDir, const glm::vec3& currentSunDir,
                                            float thresholdDeg, bool autoRebake, bool hasEnvironment,
                                            bool explicitRequest )
    {
        if ( explicitRequest )
            return true;

        // The FIRST bake is not an automatic rebake: without it the scene has no ambient light at all, and
        // "Auto Rebake off" is a request to stop re-baking, not a request to render an unlit world.
        if ( !hasEnvironment )
            return true;

        if ( !autoRebake )
            return false;

        const float baked   = glm::length( bakedSunDir );
        const float current = glm::length( currentSunDir );
        if ( baked <= 0.0f || current <= 0.0f )
            return true; // no usable baked direction to compare against

        // Clamped before acos: antipodal directions land on exactly -1 - 1e-7 in float and acos() of that
        // is NaN, which compares false against every threshold and silently disables rebaking forever.
        const float cosAngle = glm::clamp( glm::dot( bakedSunDir / baked, currentSunDir / current ), -1.0f, 1.0f );
        return glm::degrees( std::acos( cosAngle ) ) > thresholdDeg;
    }

    // How long the sun must hold STILL before a wanted rebake is allowed to run.
    inline constexpr float kSkyRebakeSettleSeconds = 0.15f;
    // ...and the longest a wanted rebake may be held back while the sun keeps moving. Without this second
    // bound a sun that never stops — which is exactly what the time-of-day driver does — would defer the
    // bake forever and freeze the environment at the hour the scene was opened at.
    inline constexpr float kSkyRebakeMaxDeferSeconds = 1.0f;

    // Whether a rebake that ShouldRebakeSkyEnvironment has already ASKED for may run this frame.
    //
    // The angular threshold decides that the environment is stale; this decides when to act on it. They
    // are different questions, and conflating them is what made dragging the sun unusable: at 5 degrees
    // per step a drag crosses the threshold several times a second, and every crossing idled the device
    // and rebuilt four cube images. Waiting for the drag to END collapses that whole gesture into one
    // bake, and the deferral bound keeps a continuously moving sun refreshing at ~1 Hz regardless.
    //
    // @p secondsSinceSunMoved counts from the last frame the sun direction actually changed;
    // @p secondsSinceStale counts from the frame the rebake was first wanted.
    inline bool SkyEnvironmentRebakeMayRun( float secondsSinceSunMoved, float secondsSinceStale,
                                            float settleSeconds, float maxDeferSeconds )
    {
        return secondsSinceSunMoved >= settleSeconds || secondsSinceStale >= maxDeferSeconds;
    }

    // ---------------------------------------------------------------------------------------------------
    // What the bake costs
    // ---------------------------------------------------------------------------------------------------

    // The IBL cube chain that every baked environment produces. These are the sizes SceneEnvironment
    // actually asks for; they live here so the cost report and the bake cannot disagree.
    inline constexpr uint32_t kSkyEnvCubeFaceSize       = 1024;
    inline constexpr uint32_t kSkyEnvIrradianceFaceSize = 32;
    inline constexpr uint32_t kSkyEnvPrefilterMips      = 11;
    inline constexpr uint32_t kSkyEnvBytesPerPixel      = 16; // RGBA32F — Image::GetBytesPerPixel

    struct SkyEnvironmentSize
    {
        uint32_t Width  = 0;
        uint32_t Height = 0;
    };

    // An ENUM, not an int: the bake dispatches in 32x32 work groups, so a hand-typed size that is not a
    // multiple of 32 would silently leave the panorama's right/bottom edge unwritten.
    inline SkyEnvironmentSize EnvironmentPanoramaSize( ECS::SkyEnvironmentResolution resolution )
    {
        switch ( resolution )
        {
            case ECS::SkyEnvironmentResolution::Low:
                return { 512u, 256u };
            case ECS::SkyEnvironmentResolution::High:
                return { 2048u, 1024u };
            case ECS::SkyEnvironmentResolution::Medium:
                break;
        }
        return { 1024u, 512u };
    }

    inline const char* EnvironmentResolutionName( ECS::SkyEnvironmentResolution resolution )
    {
        switch ( resolution )
        {
            case ECS::SkyEnvironmentResolution::Low:
                return "Low";
            case ECS::SkyEnvironmentResolution::High:
                return "High";
            case ECS::SkyEnvironmentResolution::Medium:
                break;
        }
        return "Medium";
    }

    // Bytes a cube of @p faceSize with @p mips levels occupies (6 faces, halved per mip, RGBA32F).
    inline uint64_t SkyEnvironmentCubeBytes( uint32_t faceSize, uint32_t mips )
    {
        uint64_t bytes = 0;
        for ( uint32_t mip = 0; mip < mips; ++mip )
        {
            const uint64_t side = std::max( 1u, faceSize >> mip );
            bytes += 6ull * side * side * kSkyEnvBytesPerPixel;
        }
        return bytes;
    }

    struct SkyEnvironmentCost
    {
        uint64_t PanoramaBytes = 0;
        uint64_t CubeBytes     = 0; // radiance + irradiance + prefiltered
        uint64_t TotalBytes    = 0;
    };

    // What one baked environment costs on the GPU. Note that only the PANORAMA scales with the resolution
    // ladder — the cube chain is a fixed 1024-texel face either way — which is exactly the kind of thing a
    // number in the log tells you and a tooltip does not.
    inline SkyEnvironmentCost SkyEnvironmentBakeCost( ECS::SkyEnvironmentResolution resolution )
    {
        const SkyEnvironmentSize size = EnvironmentPanoramaSize( resolution );

        SkyEnvironmentCost cost;
        cost.PanoramaBytes = static_cast<uint64_t>( size.Width ) * size.Height * kSkyEnvBytesPerPixel;
        cost.CubeBytes     = SkyEnvironmentCubeBytes( kSkyEnvCubeFaceSize, 1u ) +
                         SkyEnvironmentCubeBytes( kSkyEnvIrradianceFaceSize, 1u ) +
                         SkyEnvironmentCubeBytes( kSkyEnvCubeFaceSize, kSkyEnvPrefilterMips );
        cost.TotalBytes = cost.PanoramaBytes + cost.CubeBytes;
        return cost;
    }

    inline double BytesToMiB( uint64_t bytes )
    {
        return static_cast<double>( bytes ) / ( 1024.0 * 1024.0 );
    }

    // ---------------------------------------------------------------------------------------------------
    // Planet radius
    // ---------------------------------------------------------------------------------------------------

    // The component authors kilometres because 6360 is a number a human can check and 636000000 is not.
    // This is the ONE place the conversion to world units (centimetres) happens.
    inline float PlanetRadiusToWorldUnits( float kilometres )
    {
        return Common::Units::Metres( kilometres * 1000.0f );
    }
} // namespace Desert::Graphic

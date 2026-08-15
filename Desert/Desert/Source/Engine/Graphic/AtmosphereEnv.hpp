#pragma once

#include <Engine/Graphic/SkySettings.hpp>

#include <glm/glm.hpp>

namespace Desert::ShaderResources
{
    class StorageBuffer;
}

namespace Desert::Graphic
{
    class Image2D;
    class Image3D;

    // Per-frame, EVALUATED state of the sky — the runtime form other renderers consume via
    // SceneRenderer::GetAtmosphere(), mirroring WindEnv / GetWind(). The volumetric cloud pass is its
    // reason for existing: cloud lighting and sky lighting must come from one sun and one sky, or they
    // disagree in a way nobody finds by looking.
    //
    // THIS STRUCT IS CLOSED, and deliberately narrow. It carries EVALUATED QUANTITIES, never the authoring
    // representation they came from: no SkyAtmosphereData, no SkySettings, no palette. A consumer that
    // received the palette would be coupled to the sky's most volatile surface — every colour added,
    // reordered or reinterpreted would break its binary layout. What is shared instead is the COMPUTATION:
    // in C++ through the numbers below, and in GLSL through Common/Atmosphere.glslh's EvaluateSky.
    //
    // ParamsBuffer is consistent with that rule rather than an exception to it: a handle is not a layout.
    // A consumer binds it (`SetStorageBuffer( itsOwnBinding, env.ParamsBuffer )`) and unpacks it in GLSL
    // through the shared loaders; it can never read a field of it from C++.
    struct AtmosphereEnv
    {
        glm::vec3 SunDirection{ 0.0f, 1.0f, 0.0f }; // normalized, TOWARD the sun — the engine's one negation
        glm::vec3 SunIrradiance{ 0.0f };            // linear RGB, SunColor * SunIntensity
        glm::vec3 ZenithRadiance{ 0.0f };           // linear RGB ambient from above (day/night blended)
        glm::vec3 GroundRadiance{ 0.0f };           // linear RGB ambient from below

        float SunAngularRadius = 0.0f; // radians
        float NightFactor      = 0.0f; // 1 at night, 0 in daylight — matches Atmosphere.glslh's day blend
        float PlanetRadius     = 0.0f; // WORLD UNITS (centimetres)

        // What survives the trip from the ground to the top of the atmosphere along the sun's own
        // direction — UE's GetTransmittanceAtGroundLevel, evaluated on the CPU from the same medium the
        // transmittance LUT marches (Graphic::SunTransmittanceAtGround, which compiles SkyMedium.glslh).
        //
        // EXACTLY (1,1,1) when the coupling does not apply: the artistic-gradient model, where sky
        // radiance and surface illuminance are documented as independent, or an atmosphere sun whose
        // "Affected By Atmosphere Transmittance" is off. A consumer therefore multiplies by it
        // unconditionally and never asks which model is running.
        //
        // Its consumer is the directional light's colour (SceneRenderer::OnUpdate): in
        // SkyModel::PhysicalAtmosphere the sun that lights geometry reddens and dims by the same law
        // that reddens the sky behind it, which is the whole point of the physical model.
        glm::vec3 SunTransmittanceAtGround{ 1.0f };

        // false when there is no enabled sky component, or no atmosphere sun to drive it. A consumer that
        // draws anyway is drawing against last frame's sun.
        bool Valid = false;

        // OPAQUE handle to the packed sky-parameter SSBO. Non-owning: the SkyboxRenderer of this
        // SceneRenderer owns the buffer, and it is null exactly when Valid is false.
        ShaderResources::StorageBuffer* ParamsBuffer = nullptr;

        // OPAQUE handle to this view's camera aerial-perspective volume (32x32x16 RGBA16F), and the two
        // numbers a consumer needs to address it. Non-owning, same contract as ParamsBuffer: the
        // SkyboxRenderer of this SceneRenderer owns it, and it is NULL EXACTLY WHEN THERE IS NO AERIAL
        // PERSPECTIVE THIS FRAME — the artistic-gradient model, a sky that is switched off, or a fill
        // that could not allocate. A consumer treats null as "compose the identity", never as "sample
        // anyway and hope".
        //
        // The two scalars live here rather than in the sky payload because the volume's READER (the
        // atmospheric-fog pass) does not bind the sky buffer: this struct is then the single runtime
        // source both the fill and the read take them from, which is what stops the slice mapping's two
        // ends from drifting apart.
        Image3D* AerialPerspectiveVolume            = nullptr;
        float    AerialPerspectiveDepthKm           = 0.0f; // the volume's far extent, kilometres
        float    AerialPerspectiveViewDistanceScale = 1.0f; // read-side multiplier on a pixel's distance

        // OPAQUE handle to this frame's DISTANT SKY LIGHT — one RGBA32F texel holding the average
        // radiance of the sky seen from 6 km (UE's Distant Sky Light LUT; the fill is
        // Programs/Sky/SkyDistantLight.shader). Non-owning, same contract as the two handles above: the
        // SkyboxRenderer of this SceneRenderer owns it, and it is NULL EXACTLY WHEN THERE IS NO
        // PHYSICAL AVERAGE SKY THIS FRAME — the artistic-gradient model, a sky that is switched off, or
        // a fill that could not allocate.
        //
        // THE PHYSICAL SOURCE OF AMBIENT, and the reason it is a handle and not three floats: the value
        // is produced on the GPU and consumed on the GPU (the atmospheric-fog pass adds it to the fog's
        // in-scattering colour), so reading it into C++ would mean a readback stall for a number nobody
        // on the CPU needs.
        //
        // The dome fields above (ZenithRadiance / GroundRadiance) are the ARTISTIC-GRADIENT model's
        // ambient and are untouched by this: the clouds are calibrated against them (CLD-100/101/102),
        // and moving them onto this value is its own reviewed step (research doc section 5, Q4).
        Image2D* DistantSkyLight = nullptr;
    };

    // The C++ half of "share the computation": the quantities below are read off the same gradient the
    // shader evaluates, integrated over the directions that matter for ambient lighting.
    //
    // ZenithRadiance is, despite the historical name, the whole upper DOME: the shader's gradient blended
    // toward the horizon colour by solid angle, because a cloud's shadowed side is lit by the entire sky
    // it hangs against and the zenith texel alone is its darkest, bluest corner. The sunset band (a
    // Gaussian in elevation) and the star field (a sparse hash) are not ambient light and stay out.
    // GroundRadiance is what the ground REFLECTS — sun plus dome, times albedo over pi — not the palette
    // tone the ground is painted with; the two differ by an order of magnitude at noon.
    //
    // THESE TWO ARE THE ARTISTIC GRADIENT'S OWN AMBIENT, and that is now a scope, not a shortage: the
    // physical model's ambient is DistantSkyLight above — the real average sky, marched every frame.
    // Both live here on purpose, one per SkyModel, and the numbers below are deliberately left exactly
    // as the cloud calibration (CLD-100/101/102) measured them. Moving the clouds onto the physical
    // value is a calibration event with its own reviewed step (research doc section 5, Q4), not a side
    // effect of the value arriving.
    inline AtmosphereEnv EvaluateAtmosphere( const SkySettings& sky, const glm::vec3& towardSun,
                                             ShaderResources::StorageBuffer* paramsBuffer )
    {
        const glm::vec3 dir = glm::normalize( towardSun );

        AtmosphereEnv env;
        env.SunDirection = dir;

        // Identical blend to Atmosphere.glslh: day = smoothstep(-0.10, 0.20, sunDir.y).
        const float day = glm::smoothstep( -0.10f, 0.20f, dir.y );

        // The sun's COLOUR is a function of its elevation, and that function already exists: this is
        // Atmosphere.glslh:145 verbatim, the same tint the shader gives the solar disc and its glow.
        // Mirroring it is what keeps the promise made two comments up — that the sky and the clouds see
        // ONE sun. This used to read `sky.SunColor * sky.SunIntensity`, with no dependence on elevation
        // at all, so the sky went to sunset orange while every cloud hanging in it stayed lit by the same
        // noon white. Since SunIrradiance is read by the cloud march and nothing else, that white was
        // the whole reason a cloud could never take the colour of the light falling on it.
        const glm::vec3 sunTint = glm::mix( sky.SunsetColor, sky.SunColor, glm::smoothstep( 0.0f, 0.25f, dir.y ) );

        env.NightFactor = 1.0f - day;

        // The irradiance ramp follows the DISC, not the sky's day blend (CLD-102). The day blend spans
        // elevations up to 11.5 degrees because the sky's colour turns long before the sun sets; the
        // sun's own light does not — at 5.7 degrees it is dimmed and reddened, not one-third gone. Using
        // `day` here removed a third of the direct light from every golden-hour cloud, which is exactly
        // the hour clouds are judged in. The reddening is sunTint's job, above; this ramp only has to
        // take the light out once the disc is genuinely below the horizon.
        const float discVisibility = glm::smoothstep( -0.06f, 0.06f, dir.y );
        env.SunIrradiance          = sunTint * sky.SunIntensity * discVisibility;

        // The sky ambient a cloud receives is the DOME, not the zenith texel (CLD-100). Weighted toward
        // the horizon colour because that is where the solid angle is: for the shader's own gradient the
        // band below 45 degrees of elevation holds ~71% of the hemisphere (1 - sin 45), and the horizon
        // colour dominates it. Feeding the zenith colour alone gave the ambient an R:B ratio of 0.11 and
        // painted every shadowed cloud face navy — the deck's references (PDF pp.127/180/205) show
        // midday cloud shadows as LUMINOUS blue-grey, i.e. lit by the whole sky they hang against.
        const glm::vec3 dayDome = glm::mix( sky.ZenithColor, sky.HorizonColor, 0.65f );
        env.ZenithRadiance      = glm::mix( sky.NightColor, dayDome, day ) * sky.SkyBrightness;

        // Ground bounce is SUNLIT-ground bounce (CLD-101): the ground reflects the sun and the sky dome,
        // Lambertian, so its radiance is (sun * cos(elevation) + dome) * albedo / pi — with GroundColor
        // playing the albedo it has always visually been. The palette tone alone (~0.1 luminance) lit
        // every cloud base with a tenth of what the scene's own ground actually reflects at noon, which
        // is why undersides rendered near-black the moment the sun march gave up (PDF p.83: bases are
        // warmer AND darker than tops, but still mid-grey, never black).
        const float     invPi       = 0.3183099f;
        const glm::vec3 groundLight = env.SunIrradiance * glm::max( dir.y, 0.0f ) + env.ZenithRadiance;
        env.GroundRadiance          = glm::mix( 0.30f, 1.0f, day ) * sky.GroundColor * groundLight * invPi;

        env.SunAngularRadius = sky.SunAngularRadius;
        env.PlanetRadius     = sky.PlanetRadius;
        env.Valid            = true;
        env.ParamsBuffer     = paramsBuffer;
        return env;
    }
} // namespace Desert::Graphic

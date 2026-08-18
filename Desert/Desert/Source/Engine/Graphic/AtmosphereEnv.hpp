#pragma once

#include <Engine/Graphic/SkySettings.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    class Image2D;
    class Image3D;

    // Per-frame, EVALUATED state of the sky — the runtime form other renderers consume via
    // SceneRenderer::GetAtmosphere(), mirroring WindEnv / GetWind(). Its reason for existing is that the
    // sky's lighting and every other pass's lighting must come from one sun and one sky, or they disagree
    // in a way nobody finds by looking.
    //
    // THIS STRUCT IS CLOSED, and deliberately narrow. It carries EVALUATED QUANTITIES, never the authoring
    // representation they came from: no SkyAtmosphereData, no SkySettings, no palette. A consumer that
    // received the palette would be coupled to the sky's most volatile surface — every colour added,
    // reordered or reinterpreted would break its binary layout. What is shared instead is the COMPUTATION:
    // in C++ through the numbers below, and in GLSL through Common/Atmosphere.glslh's EvaluateSky.
    struct AtmosphereEnv
    {
        glm::vec3 SunDirection{ 0.0f, 1.0f, 0.0f }; // normalized, TOWARD the sun — the engine's one negation
        glm::vec3 SunIrradiance{ 0.0f };            // linear RGB, SunColor * SunIntensity
        glm::vec3 ZenithRadiance{ 0.0f };           // linear RGB ambient from above (day/night blended)
        glm::vec3 GroundRadiance{ 0.0f };           // linear RGB ambient from below

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

        // UE's AtmosphereLightIlluminanceOnGroundPostTransmittance: the sun LIGHT's own illuminance —
        // the directional light's authored Color x Intensity — after the atmosphere has taken its cut.
        // Exactly the product of the light's outer-space illuminance and the transmittance above, so it
        // is the same number SceneRenderer::OnUpdate arrives at for the light's colour, computed once
        // here rather than twice.
        //
        // WHY IT IS NOT SunIrradiance. SunIrradiance is the SKY's sun — the sky component's own
        // SunColor x SunIntensity, elevation-tinted, which says how bright the disc in the sky looks.
        // This is what the sun DOES TO SURFACES, authored on the directional light. In a scene with a sky
        // SunIntensity of 22 and a light Intensity of 1 they differ by more than an order of magnitude,
        // and a consumer that wants "the light on the ground" must not read the other one.
        //
        // Its consumer is the fog's directional in-scattering lobe (Graphic::PackFogParams), in
        // SkyModel::PhysicalAtmosphere only — the gradient's fog keeps the sky's sun, which is what it
        // was calibrated against.
        glm::vec3 SunIlluminanceOnGround{ 0.0f };

        // false when there is no enabled sky component, or no atmosphere sun to drive it. A consumer that
        // draws anyway is drawing against last frame's sun.
        bool Valid = false;

        // OPAQUE handle to this view's camera aerial-perspective volume (32x32x16 RGBA16F), and the two
        // numbers a consumer needs to address it. Non-owning, same contract as ParamsBuffer: the
        // SkyboxRenderer of this SceneRenderer owns it, and it is NULL EXACTLY WHEN THERE IS NO AERIAL
        // PERSPECTIVE THIS FRAME — the artistic-gradient model, a sky that is switched off, or a fill
        // that could not allocate. A consumer treats null as "compose the identity", never as "sample
        // anyway and hope".
        //
        // The two scalars live here rather than in the sky payload because the volume's READER (the
        // atmospheric-fog pass) does not bind the sky parameter buffer: this struct is then the single
        // runtime source both the fill and the read take them from, which is what stops the slice
        // mapping's two ends from drifting apart.
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
        // ambient and are untouched by this: one per SkyModel, on purpose.
        Image2D* DistantSkyLight = nullptr;
    };

    // The C++ half of "share the computation": the quantities below are read off the same gradient the
    // shader evaluates, integrated over the directions that matter for ambient lighting.
    //
    // ZenithRadiance is, despite the historical name, the whole upper DOME: the shader's gradient blended
    // toward the horizon colour by solid angle, because a surface lit by ambient is lit by the entire sky
    // above it and the zenith texel alone is its darkest, bluest corner. The sunset band (a Gaussian in
    // elevation) and the star field (a sparse hash) are not ambient light and stay out. GroundRadiance is
    // what the ground REFLECTS — sun plus dome, times albedo over pi — not the palette tone the ground is
    // painted with; the two differ by an order of magnitude at noon.
    //
    // THESE TWO ARE THE ARTISTIC GRADIENT'S OWN AMBIENT, and that is a scope, not a shortage: the
    // physical model's ambient is DistantSkyLight above — the real average sky, marched every frame.
    // Both live here on purpose, one per SkyModel.
    inline AtmosphereEnv EvaluateAtmosphere( const SkySettings& sky, const glm::vec3& towardSun )
    {
        const glm::vec3 dir = glm::normalize( towardSun );

        AtmosphereEnv env;
        env.SunDirection = dir;

        // Identical blend to Atmosphere.glslh: day = smoothstep(-0.10, 0.20, sunDir.y).
        const float day = glm::smoothstep( -0.10f, 0.20f, dir.y );

        // The sun's COLOUR is a function of its elevation, and that function already exists: this is
        // Atmosphere.glslh:145 verbatim, the same tint the shader gives the solar disc and its glow.
        // Mirroring it is what keeps the promise made two comments up — that every consumer sees ONE sun.
        // This used to read `sky.SunColor * sky.SunIntensity`, with no dependence on elevation at all, so
        // the sky went to sunset orange while everything lit by this irradiance stayed noon white.
        const glm::vec3 sunTint = glm::mix( sky.SunsetColor, sky.SunColor, glm::smoothstep( 0.0f, 0.25f, dir.y ) );

        // The irradiance ramp follows the DISC, not the sky's day blend. The day blend spans elevations
        // up to 11.5 degrees because the sky's colour turns long before the sun sets; the sun's own light
        // does not — at 5.7 degrees it is dimmed and reddened, not one-third gone. Using `day` here
        // removed a third of the direct light from the whole golden hour. The reddening is sunTint's job,
        // above; this ramp only has to take the light out once the disc is genuinely below the horizon.
        const float discVisibility = glm::smoothstep( -0.06f, 0.06f, dir.y );
        env.SunIrradiance          = sunTint * sky.SunIntensity * discVisibility;

        // The sky ambient is the DOME, not the zenith texel. Weighted toward the horizon colour because
        // that is where the solid angle is: for the shader's own gradient the band below 45 degrees of
        // elevation holds ~71% of the hemisphere (1 - sin 45), and the horizon colour dominates it.
        // Feeding the zenith colour alone gave the ambient an R:B ratio of 0.11 and painted every
        // shadowed surface navy.
        const glm::vec3 dayDome = glm::mix( sky.ZenithColor, sky.HorizonColor, 0.65f );
        env.ZenithRadiance      = glm::mix( sky.NightColor, dayDome, day ) * sky.SkyBrightness;

        // Ground bounce is SUNLIT-ground bounce: the ground reflects the sun and the sky dome,
        // Lambertian, so its radiance is (sun * cos(elevation) + dome) * albedo / pi — with GroundColor
        // playing the albedo it has always visually been. The palette tone alone (~0.1 luminance) is a
        // tenth of what the scene's own ground actually reflects at noon.
        const float     invPi       = 0.3183099f;
        const glm::vec3 groundLight = env.SunIrradiance * glm::max( dir.y, 0.0f ) + env.ZenithRadiance;
        env.GroundRadiance          = glm::mix( 0.30f, 1.0f, day ) * sky.GroundColor * groundLight * invPi;

        env.Valid = true;
        return env;
    }
} // namespace Desert::Graphic

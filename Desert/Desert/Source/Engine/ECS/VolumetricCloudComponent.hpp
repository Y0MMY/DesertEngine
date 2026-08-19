#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

#include <Engine/Reflection/ReflectionMacros.hpp>

#include <cstdint>

namespace Desert::ECS
{
    // The volumetric cloud layer: a spherical shell around the planet, marched per pixel by
    // Graphic::System::VolumetricCloudRenderer.
    //
    // WHERE THE PARAMETERS COME FROM. The layer geometry and the tracing limits are
    // UVolumetricCloudComponent's, name for name, so a UE-calibrated sky transplants number for number
    // (licence position: Docs/Clouds/LICENCE_RECORD.md). The shape parameters are ours, because UE has
    // none — in UE the density is a material graph the artist authors, and there is no cloud-shape
    // parameter on the component at all. The shape maths itself follows Nubis (SIGGRAPH 2023): the
    // vertical profile multiplied by a coverage field (deck p.19), eroded by a remap rather than a
    // subtraction (p.120).
    //
    // EVERY FIELD BELOW IS READ, and it reaches the GPU by one of two routes rather than one. Most of the
    // fields are packed into Graphic::CloudGpuPayload — TWELVE vec4s, that is 48 floats and 192 bytes,
    // with no padding and no reserved slot. The four bake settings (the two seeds and the two octave
    // counts) are NOT in that block: they decide what the noise volume CONTAINS rather than how the march
    // reads it, so they travel as Graphic::CloudNoiseBakeKey, four unsigned words riding a push constant,
    // which doubles as the key that decides whether to rebake. Either way each field has a named consumer,
    // and Desert/Tests/Engine/SettingConsumers names it. The contract forbids a knob that moves nothing,
    // and this component is where that is easiest to get wrong, because a cloud parameter that does
    // nothing still LOOKS like it does when the sky is already busy.
    //
    // UNITS. Distances are world units — centimetres (Length) — and are converted to kilometres exactly
    // once, in Graphic::PackCloudParams. The planet radius is the one exception and is authored in
    // kilometres, because 6360 km is 636 000 000 cm and no slider is useful at that scale; UE authors it
    // in kilometres for the same reason.
    //
    // ALTITUDES ARE ANCHORED TO METEOROLOGY, not to a relation. The default base of 1.5 km is a cumulus
    // mediocris base (real ones sit at 0.8-2.0 km), and the accompanying test asserts the range rather
    // than a ratio. A system whose altitudes are only constrained relative to its own tile sizes is one
    // that can be moved wholesale into the stratosphere without a single test noticing.
    struct VolumetricCloudData
    {
        REFLECT()

        // ---- Cloud Layer ----------------------------------------------------------------------------

        PROPERTY( DisplayName( "Enabled" ), Category( "Cloud Layer" ), Summary,
                  Tooltip( "Master switch. Off dispatches nothing: a scene with the clouds disabled pays "
                           "zero GPU cost, exactly like a scene without the component." ) )
        bool Enabled = true;

        PROPERTY( DisplayName( "Layer Bottom Altitude" ), Category( "Cloud Layer" ), Length,
                  Range( 0.0f, 2000000.0f ), Summary,
                  Tooltip( "Height of the cloud base above the ground. It does not only move the layer up "
                           "— together with Weather Tile Size it decides how BIG a cloud looks: the "
                           "angle one subtends overhead is its cell size over this altitude." ) )
        // FIVE KILOMETRES — UE's shipped default, and it is safe to be this high only because the layer
        // is an envelope rather than the cloud (see LayerThickness). The apparent size of a cloud overhead
        // is its coverage cell over this altitude, so raising one without the other is how a sky ends up
        // correct in every relation and wrong in every frame.
        float LayerBottomAltitude = 500000.0f; // 5 km

        PROPERTY( DisplayName( "Layer Thickness" ), Category( "Cloud Layer" ), Length,
                  Range( 10000.0f, 2000000.0f ),
                  Tooltip( "Vertical extent of the layer. The cloud type decides how much of it a cloud "
                           "actually fills; this is the ceiling, not the height of the cloud." ) )
        // TEN KILOMETRES, matching UE, and the reason it is safe to be this generous is that the layer is
        // an ENVELOPE, not the cloud. UE's height-profile texture confines a stratocumulus to a small
        // fraction of these ten kilometres; the thickness only says how much room the tallest species is
        // allowed. Our vertical profile now does the same — see CloudVerticalProfile, whose occupancy was
        // cut to a fifth when this changed.
        float LayerThickness = 1000000.0f; // 10 km

        PROPERTY( DisplayName( "Planet Radius" ), Category( "Cloud Layer" ), Units( "km" ),
                  Range( 100.0f, 7000.0f ), Advanced,
                  Tooltip( "Radius of the planet the layer curves around. It is what puts the horizon "
                           "where it belongs: a flat layer has no horizon at all and either fills the "
                           "whole lower sky or ends at an invisible edge." ) )
        float PlanetRadius = 6360.0f;

        PROPERTY( DisplayName( "Max View Distance" ), Category( "Cloud Layer" ), Length,
                  Range( 100000.0f, 40000000.0f ),
                  Tooltip( "How far along the ray the march may run, measured FROM THE POINT THE RAY "
                           "ENTERS THE LAYER. Measured from the camera instead it would cut the layer at "
                           "a fixed radius and draw a circular edge across the sky." ) )
        // SIXTY KILOMETRES, and it is half of a PAIR rather than a number of its own. Divided by
        // WeatherTileSize it gives the number of times the coverage field REPEATS between the camera and
        // the vanishing point, and a repeating field seen end-on reads as streaks radiating from that
        // point. Docs/Clouds/CALIBRATION.md section 4 records the failure and its cure: at 150 km against
        // an 8 km tile the field repeated about twenty times and the moire was unmissable, and the pair
        // that fixed it was 60 km against 12 km — five repeats. These defaults ARE that pair, and
        // ComponentReflection asserts the ratio rather than the two numbers, because it is the ratio that
        // was measured.
        float MaxViewDistance = 6000000.0f; // 60 km

        PROPERTY( DisplayName( "Tracing Start Max Distance" ), Category( "Cloud Layer" ), Length,
                  Range( 1000000.0f, 100000000.0f ), Advanced,
                  Tooltip( "If a ray only enters the layer beyond this distance, it is not traced at all. "
                           "It bounds the cost of grazing rays, and it is the guard that keeps a ray whose "
                           "entry the geometry reports thousands of kilometres away from ever being "
                           "marched." ) )
        float TracingStartMaxDistance = 35000000.0f; // 350 km — UE's shipped default

        PROPERTY( DisplayName( "Tracing Start Distance" ), Category( "Cloud Layer" ), Length,
                  Range( 0.0f, 5000000.0f ), Advanced,
                  Tooltip( "Pushes the start of the march away from the camera. Useful when the camera "
                           "is inside the layer and the nearest metres are both the most expensive and "
                           "the least interesting." ) )
        float TracingStartDistance = 0.0f;

        // ---- Weather --------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Coverage" ), Category( "Weather" ), Range( 0.0f, 1.0f ), Summary,
                  Tooltip( "How much of the sky has cloud in it. Applied as a THRESHOLD on the coverage "
                           "field, so lowering it opens clear gaps rather than thinning everything." ) )
        // 0.25, MEASURED — and the metric is stated, which the first version of this table was not.
        //
        // "sky cover" below is the fraction of vertical COLUMNS through the layer that contain any
        // renderable density, over one period of the coverage field at contrast 1 with the defaults of
        // this component. Desert/Tests/Engine/CloudField measures it and prints exactly this row, so it
        // is reproducible rather than remembered — the figures here are the ones that suite emitted after
        // the weather tile moved onto the calibrated 12 km:
        //
        //     Coverage   0.00   0.10   0.20   0.30   0.40   0.50
        //     sky cover     0%     11%    48%    85%    96%   100%
        //
        // Both ends are exact by construction, which they were NOT before: the threshold spans the field's
        // whole range, so 0 is genuinely clear and 1 genuinely solid. The useful band is 0.15 to 0.35 —
        // the curve is steep because a slanted ray crosses many columns, and that steepness is a property
        // of the geometry rather than of the slider.
        float Coverage = 0.25f;

        PROPERTY( DisplayName( "Coverage Contrast" ), Category( "Weather" ), Range( 0.1f, 4.0f ),
                  Tooltip( "Sharpness of the transition from clear to cloudy, as the WIDTH of the band "
                           "over which the coverage field turns into cloud. Above 1 the band narrows and "
                           "the islands get hard edges; below 1 it widens and they melt into haze." ) )
        float CoverageContrast = 1.0f;

        PROPERTY( DisplayName( "Cloud Type" ), Category( "Weather" ), Range( 0.0f, 1.0f ), Summary,
                  Tooltip( "0 is a flat sheet lying in the bottom quarter of the layer; 1 is a heaped "
                           "cloud that fills it. Drives the vertical profile and the erosion frequency "
                           "together, because a stratus and a cumulus differ in both." ) )
        float CloudType = 0.6f;

        PROPERTY( DisplayName( "Cloud Type Variance" ), Category( "Weather" ), Range( 0.0f, 1.0f ),
                  Tooltip( "How far the cloud type wanders from the value above across the sky. AT ZERO "
                           "EVERY CLOUD IN THE LAYER REACHES THE SAME ALTITUDE, because the vertical "
                           "profile is then the same function everywhere and the layer reads as a slab "
                           "with a lid. Raising it lets one island be a low sheet and its neighbour a "
                           "tower, and gives a single large island an uneven top." ) )
        float CloudTypeVariance = 0.4f;

        PROPERTY( DisplayName( "Weather Tile Size" ), Category( "Weather" ), Length,
                  Range( 200000.0f, 8000000.0f ),
                  Tooltip( "World size over which the coverage field repeats. Its coarsest cell is a "
                           "QUARTER of it, and that cell is the size of one cloud — which is also what "
                           "decides whether there is any cloud overhead at all: a cell much larger than "
                           "the layer altitude cannot fit one above the camera, and the zenith comes out "
                           "empty however high the coverage is set." ) )
        // TWELVE KILOMETRES -> 3 km cells, a cumulus field. The other half of the calibrated pair; see
        // MaxViewDistance for what the two of them together decide and for where it was measured.
        float WeatherTileSize = 1200000.0f; // 12 km -> 3 km cells, a cumulus field

        PROPERTY( DisplayName( "Weather Seed" ), Category( "Weather" ), Range( 0, 100000 ), Advanced,
                  Tooltip( "Changes which clouds the coverage field produces without changing their "
                           "statistics. Rebakes the noise volume." ) )
        int32_t WeatherSeed = 1337;

        PROPERTY( DisplayName( "Weather Octaves" ), Category( "Weather" ), Range( 1, 6 ), Advanced,
                  Tooltip( "How many octaves of the coverage field are summed. More octaves means more "
                           "irregular island outlines and a proportionally more expensive bake." ) )
        int32_t WeatherOctaves = 3;

        // ---- Detail ---------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Detail Tile Size" ), Category( "Detail" ), Length, Range( 20000.0f, 3000000.0f ),
                  Tooltip( "World size over which the erosion field repeats — the scale of the billows "
                           "and wisps cut into the cloud's edge." ) )
        float DetailTileSize = 400000.0f; // 4 km

        PROPERTY( DisplayName( "Detail Strength" ), Category( "Detail" ), Range( 0.0f, 1.0f ),
                  Tooltip( "How deeply the erosion cuts. At 0 the cloud keeps the smooth silhouette of "
                           "its coverage field; at 1 the edge is eaten away into wisps." ) )
        // 0.10, DOWN FROM 0.5, and the correction comes from UE's own numbers: its base noise strength
        // is 0.8 and its detail strength is 0.08 — the erosion is an order of magnitude weaker than the
        // shape it cuts into. At 0.5 the erosion was removing most of the layer and leaving a veil, which
        // is the symptom I chased for several iterations before this reference arrived.
        float DetailStrength = 0.1f;

        PROPERTY( DisplayName( "Detail Seed" ), Category( "Detail" ), Range( 0, 100000 ), Advanced,
                  Tooltip( "Seeds the erosion field independently of the coverage field, so the two "
                           "cannot produce correlated structure. Rebakes the noise volume." ) )
        int32_t DetailSeed = 13;

        PROPERTY( DisplayName( "Detail Octaves" ), Category( "Detail" ), Range( 1, 6 ), Advanced,
                  Tooltip( "How many octaves of the erosion field are summed." ) )
        int32_t DetailOctaves = 2;

        PROPERTY( DisplayName( "Density Scale" ), Category( "Detail" ), Range( 0.0f, 2.0f ),
                  Tooltip( "Multiplies the eroded density. Below 1 the whole layer thins toward haze; "
                           "above 1 the thin edges fill in." ) )
        float DensityScale = 1.0f;

        PROPERTY( DisplayName( "Extinction Scale" ), Category( "Detail" ), Units( "/km" ), Range( 0.5f, 60.0f ),
                  Tooltip( "How strongly the medium absorbs and scatters, per kilometre at full density. "
                           "This is what makes a cloud opaque rather than merely visible." ) )
        // EIGHT, NOT FORTY-FIVE, and the difference is the approximation rather than the physics. A real
        // cumulus extinguishes at roughly 45 per kilometre, at which the optical depth toward the sun is
        // ~25 everywhere inside the body and EVERY scattering order arrives at zero — the cloud renders
        // uniformly grey. What makes a real one white is a random walk of photons at an albedo of
        // 0.9999, which three octaves do not reproduce and are not meant to. This is therefore the
        // EFFECTIVE extinction of the approximation: chosen so that a kilometre of cloud is opaque along
        // the view ray while the shadow ray still resolves a lit top and a darker base. First set from a
        // guess of 25 and corrected against the frame, which is the only instrument that measures it.
        float ExtinctionScale = 8.0f;

        PROPERTY( DisplayName( "Near Fade Start Distance" ), Category( "Detail" ), Length,
                  Range( 0.0f, 2000000.0f ), Advanced,
                  Tooltip( "Where the near-camera fade begins. A camera that enters the layer otherwise "
                           "meets a wall of density at arm's length; UE fades the nearest metres out for "
                           "the same reason. The fade is OFF unless End is strictly past Start." ) )
        float NearFadeStartDistance = 0.0f;

        PROPERTY( DisplayName( "Near Fade End Distance" ), Category( "Detail" ), Length, Range( 0.0f, 2000000.0f ),
                  Advanced,
                  Tooltip( "Where the near-camera fade is complete and the cloud is at full density. It "
                           "must lie strictly past Start; at or below it the pair describes no interval "
                           "and the fade is switched OFF rather than guessed at." ) )
        // THE TWO ARE ONE SETTING. Graphic::CloudResolveNearFade repairs them as a pair, because the march
        // evaluates smoothstep(Start, End, t) and GLSL leaves that undefined unless End is strictly past
        // Start — and each of the two is individually legal at any value its own slider allows.
        float NearFadeEndDistance = 0.0f;

        // ---- Lighting -------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Scattering Albedo" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  Tooltip( "Fraction of extinguished light that is scattered rather than absorbed. Water "
                           "droplets barely absorb at all, which is why clouds are white; values much "
                           "below 1 read as smoke." ) )
        // 0.98 — the value UE's shipped instance carries (Cloud_AlbedoColor). Water droplets barely
        // absorb, and the difference between 0.95 and 0.98 is not three per cent: it compounds over
        // every scattering order, so the lower value reads as a cloud made of smoke.
        float ScatteringAlbedo = 0.98f;

        PROPERTY( DisplayName( "Phase G" ), Category( "Lighting" ), Range( -0.9f, 0.9f ),
                  Tooltip( "Asymmetry of the Henyey-Greenstein phase function. Positive scatters forward, "
                           "which is what puts the bright rim on a cloud you are looking at through the "
                           "sun." ) )
        // 0.8 — UE's shipped value (Phase_Controls.r), paired with the second lobe below.
        float PhaseG = 0.8f;

        PROPERTY( DisplayName( "Phase G Backward" ), Category( "Lighting" ), Range( -0.9f, 0.9f ),
                  Tooltip( "Asymmetry of the SECOND phase lobe. Near zero it is almost isotropic, which is "
                           "what carries the body of the cloud while the first lobe carries the bright rim "
                           "against the sun. One lobe cannot do both: strong enough for the rim leaves the "
                           "body black, weak enough for the body loses the rim." ) )
        float PhaseGBackward = 0.1667f;

        PROPERTY( DisplayName( "Phase Blend" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  Tooltip( "How much of the second lobe is mixed in. UE's shipped instance weights it "
                           "toward the BODY at 0.575, so more than half the answer is the near-isotropic "
                           "lobe and the sharp one is a highlight on top." ) )
        float PhaseBlend = 0.575f;

        PROPERTY( DisplayName( "Ambient Occlusion Strength" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  Tooltip( "How strongly the depth inside the cloud darkens the ambient it receives. At 0 "
                           "the core of a three-kilometre cumulus is lit as brightly as a wisp on its edge, "
                           "which reads as a flat white cut-out." ) )
        // 0.5 — the amount UE carries in the alpha of Cloud_AlbedoColor.
        float AmbientOcclusionStrength = 0.5f;

        PROPERTY( DisplayName( "Light March Distance" ), Category( "Lighting" ), Length,
                  Range( 10000.0f, 2000000.0f ),
                  Tooltip( "How far toward the sun the shadow ray marches. Short rays light the cloud "
                           "flatly because they never leave its own body; long ones cost proportionally "
                           "more per sample, and past the thickness of the layer they buy nothing at all "
                           "because the ray has already left it." ) )
        // FIFTEEN KILOMETRES, which is UE's ShadowTracingDistance, and the previous 500 m was the reason
        // the clouds were flat. A shadow ray started inside a two-kilometre cloud and only 500 m long
        // never leaves it: every sample inside the body sees roughly the same optical depth, so the body
        // is shaded uniformly and reads as a cut-out. At fifteen kilometres a sample near the top exits
        // into clear air almost at once and is bright, while one near the base has the whole cloud above
        // it and is dark — and that difference IS the shape.
        //
        // It is not thirty times the cost: the samples are placed on a squared distribution, so the first
        // few still land in the metres nearest the sample and the far ones are cheap corrections.
        float LightMarchDistance = 1500000.0f; // 15 km

        PROPERTY( DisplayName( "Light March Samples" ), Category( "Lighting" ), Range( 1, 16 ),
                  Tooltip( "Samples along the shadow ray. They are placed on a squared distribution, so "
                           "the first few carry most of the answer and adding more has sharply "
                           "diminishing returns." ) )
        int32_t LightMarchSamples = 6;

        PROPERTY( DisplayName( "Multiple Scattering Octaves" ), Category( "Lighting" ), Range( 1, 3 ),
                  Tooltip( "How many scattering orders are approximated. ONE IS SINGLE SCATTERING, and a "
                           "cloud lit by single scattering alone is physically grey: the light that makes "
                           "a real cloud white has bounced many times inside it. Two or three is where it "
                           "starts to look like weather." ) )
        int32_t MultiScatterOctaves = 3;

        PROPERTY( DisplayName( "Multiple Scattering Contribution" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  Advanced,
                  Tooltip( "How much each successive scattering order contributes. The factor is SQUARED "
                           "at every octave, so the series falls away quickly and the third order is "
                           "already a small correction." ) )
        // 0.667 — the value UE's shipped instance carries (Multiscatter_Controls.r).
        float MultiScatterContribution = 0.667f;

        PROPERTY( DisplayName( "Multiple Scattering Occlusion" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  Advanced,
                  Tooltip( "How much less each successive order is absorbed. This is what lets light that "
                           "has already scattered reach the core of a cloud that the direct beam never "
                           "gets into — the reason a thick cumulus glows rather than going black." ) )
        // 0.25 — UE's value (Multiscatter_Controls.g), and the single largest discrepancy the reference
        // exposed. At 0.5 each successive scattering order was absorbed twice as hard as it should be,
        // so light never reached the core and the cloud read grey rather than white. Halving it is what
        // lets the deeper orders light the body from inside.
        float MultiScatterOcclusion = 0.25f;

        PROPERTY( DisplayName( "Multiple Scattering Eccentricity" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  Advanced,
                  Tooltip( "How much directionality each successive order keeps. Light that has bounced "
                           "many times has forgotten where it came from, so the higher orders blend "
                           "toward an isotropic phase." ) )
        // 0.18 — UE's value (Multiscatter_Controls.b). The higher orders go almost straight to isotropic,
        // which is physically right: light that has bounced twice has forgotten the sun.
        float MultiScatterEccentricity = 0.18f;

        PROPERTY( DisplayName( "Aerial Perspective Start Distance" ), Category( "Lighting" ), Length,
                  Range( 0.0f, 20000000.0f ), Advanced,
                  Tooltip( "Distance before the atmosphere begins to haze the clouds. At 0 — the physical "
                           "answer, and UE's default — ninety kilometres of air erases a cloud on the "
                           "horizon completely, which is correct and is not always what a sky is wanted to "
                           "look like. Pushing it out keeps the distant band visible." ) )
        float AerialPerspectiveStartDistance = 0.0f;

        PROPERTY( DisplayName( "Aerial Perspective Fade Distance" ), Category( "Lighting" ), Length,
                  Range( 0.0f, 20000000.0f ), Advanced,
                  Tooltip( "Distance over which the haze reaches full strength once it has started. Zero "
                           "applies it in full immediately." ) )
        float AerialPerspectiveFadeDistance = 0.0f;

        PROPERTY( DisplayName( "Ambient Scale" ), Category( "Lighting" ), Color,
                  Tooltip( "Scales the sky's ambient contribution to the clouds. White is the full "
                           "contribution; black lights them by the sun alone and leaves their shadowed "
                           "sides black." ) )
        glm::vec3 AmbientScale = { 1.0f, 1.0f, 1.0f };

        // ---- Quality --------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Max Steps" ), Category( "Quality" ), Range( 8, 512 ),
                  Tooltip( "Ceiling on the number of samples along a view ray. The count itself rises "
                           "with the length of the segment inside the layer and saturates at this "
                           "value, so it is a cost ceiling rather than a fixed cost." ) )
        // 256, WHICH IS AFFORDABLE ONLY BECAUSE OF THE SKIP. The march has two step sizes and spends the
        // coarse one — four times longer — on empty sky, so a ray that crosses mostly clear air finishes
        // in a quarter of these iterations. Raising the ceiling therefore buys resolution INSIDE cloud
        // without charging for it outside, which is the opposite of what raising it did before the skip
        // existed.
        int32_t MaxSteps = 256;

        PROPERTY( DisplayName( "Stop Transmittance" ), Category( "Quality" ), Range( 0.0f, 0.2f ), Advanced,
                  Tooltip( "The march stops once this little light is still getting through. Raising it "
                           "is usually the cheapest saving available, because the samples it skips are "
                           "behind material that has already hidden them." ) )
        float StopTransmittance = 0.005f;

        // ---- Animation ------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Wind Direction" ), Category( "Animation" ),
                  Tooltip( "Direction the layer drifts. Normalized by the renderer; a zero vector simply "
                           "leaves the sky still." ) )
        glm::vec3 WindDirection = { 1.0f, 0.0f, 0.0f };

        PROPERTY( DisplayName( "Wind Speed" ), Category( "Animation" ), Length, Range( 0.0f, 50000.0f ),
                  Tooltip( "How fast the layer drifts, in world units per second. The wind moves the "
                           "SAMPLE POSITION rather than the data, which is what makes the motion free and "
                           "seamless." ) )
        float WindSpeed = 3000.0f; // 30 m/s
    };

    struct VolumetricCloudComponent
    {
        VolumetricCloudData Data;
    };
} // namespace Desert::ECS

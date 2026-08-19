#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

#include <Engine/Assets/Common.hpp>
#include <Engine/Graphic/Clouds/CloudProfileTable.hpp>
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
    // EVERY FIELD BELOW IS READ, and it reaches the GPU by one of two routes rather than one. Every scalar
    // is packed into Graphic::CloudGpuPayload — TWELVE vec4s, that is 48 floats and 192 bytes, with no
    // padding and no reserved slot. The one field that is not a scalar is NoiseVolume, which names an
    // asset: Graphic::System::VolumetricCloudRenderer resolves the handle, uploads the container's voxels
    // into an Image3D and binds it. Either way each field has a named consumer, and
    // Desert/Tests/Engine/SettingConsumers names it. The contract forbids a knob that moves nothing, and
    // this component is where that is easiest to get wrong, because a cloud parameter that does nothing
    // still LOOKS like it does when the sky is already busy.
    //
    // UNITS. Distances are world units — centimetres (Length) — and are converted to kilometres exactly
    // once, in Graphic::PackCloudParams. The planet radius is the one exception and is authored in
    // kilometres, because 6360 km is 636 000 000 cm and no slider is useful at that scale; UE authors it
    // in kilometres for the same reason.
    //
    // ALTITUDES ARE ANCHORED TO METEOROLOGY, not to a relation, and they are NOT AUTHORED HERE. A species
    // carries its own base and top in kilometres (Graphic::CloudSpeciesShapeOf), and the shell the march
    // intersects is COMPUTED from them by Graphic::PackCloudParams. The two fields that used to state it —
    // Layer Bottom Altitude and Layer Thickness — are gone, and their removal is the point rather than a
    // tidy-up: an envelope and a species' altitudes are two numbers obliged to agree, which is the exact
    // class of defect §2.3.1 of the contract is about. Nothing that has to agree with the species is left
    // for a hand to move.
    struct VolumetricCloudData
    {
        REFLECT()

        // ---- Cloud Layer ----------------------------------------------------------------------------

        PROPERTY( DisplayName( "Enabled" ), Category( "Cloud Layer" ), Summary,
                  Tooltip( "Master switch. Off dispatches nothing: a scene with the clouds disabled pays "
                           "zero GPU cost, exactly like a scene without the component." ) )
        bool Enabled = true;

        PROPERTY( DisplayName( "Species" ), Category( "Cloud Layer" ), Summary,
                  Tooltip( "Which kind of cloud this layer is made of. It is not a look-up of presets: the "
                           "species carries its own base and top in kilometres, its own family of vertical "
                           "profiles, its own edge character and its own density, and the shell the march "
                           "intersects is COMPUTED from it. Changing this moves the clouds to the altitude "
                           "that species actually lives at." ) )
        // CONGESTUS IS THE DEFAULT because it is what the previous scalar's default of 0.6 described — "a
        // heaped cloud that fills the layer" — and because it is the species whose family of profiles
        // varies most across a patch, which is what a new scene should show off rather than hide.
        Graphic::CloudSpecies Species = Graphic::CloudSpecies::CumulusCongestus;

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
        // 0.10, MEASURED — and the metric is stated, which the first version of this table was not.
        //
        // "sky cover" below is the fraction of vertical COLUMNS through the layer whose cloud hides at
        // least half the sky behind it, over one period of the coverage field at contrast 1 with the
        // defaults of this component. Desert/Tests/Engine/CloudField measures it and prints exactly this
        // row, so it is reproducible rather than remembered:
        //
        //     Coverage   0.05   0.10   0.15   0.20   0.30   0.50
        //     sky cover    17%    36%    49%    60%    75%    95%
        //
        // THE ROW MOVED WITH THE ENVELOPE, and the default moved with the row. The envelope used to be an
        // authored ten kilometres of which a cloud filled a fraction; it is now the SPECIES' OWN [base,
        // top] and the shipped congestus fills all 3.6 km of it, so a column that carries cloud carries
        // three times as much of it. 0.10 is the setting that reproduces the ~40 % sky cover the previous
        // default of 0.15 produced — the slider means something different and the sky looks the same,
        // which is the same kind of move, for the same kind of reason, as the one recorded below.
        //
        // THE SLIDER STOPPED BEING A LEVEL AND BECAME A QUANTILE, and the default moved once, by exactly
        // the amount needed to leave the sky where it was. Common/CloudField.glslh now maps the coverage
        // field through its own cumulative distribution before thresholding it, so a setting selects a
        // FRACTION OF THE FIELD rather than a value of it — which is what makes the slider mean the same
        // thing whatever volume is in the Noise Volume slot, the property this component needs now that
        // the slot takes an artist's asset. The consequence here is arithmetic: the same setting selects
        // about three times as much field as before, so the useful band moved down and the default with
        // it. 0.15 is the point that produces the 40% sky cover the old 0.25 produced against the old
        // Perlin-Worley field (its row read 20% at 0.20 and 58% at 0.30) — the default is unchanged in
        // what it LOOKS like and changed in what it reads.
        //
        // Both ends are still exact by construction: the mapped field reaches 0 and 1, and the threshold
        // is pushed past both, so 0 is genuinely clear and 1 genuinely solid. The useful band is now 0.03
        // to 0.25 — the curve is steep because a slanted ray crosses many columns, and that steepness is a
        // property of the geometry rather than of the slider.
        //
        // THE THREE SHIPPED CLOUD SCENES WERE NOT RE-AUTHORED. Their Coverage is an authored value and all
        // three still read as cloud; Clouds_Showcase and Clouds_Sunset simply read fuller than they did.
        // Migrating an authored number to preserve an appearance is a guess about intent, and this
        // component is not the place to make it.
        float Coverage = 0.10f;

        PROPERTY( DisplayName( "Coverage Contrast" ), Category( "Weather" ), Range( 0.1f, 4.0f ),
                  Tooltip( "Sharpness of the transition from clear to cloudy, as the WIDTH of the band "
                           "over which the coverage field turns into cloud. Above 1 the band narrows and "
                           "the islands get hard edges; below 1 it widens and they melt into haze." ) )
        float CoverageContrast = 1.0f;

        // THE TWO FIELDS THAT USED TO STAND HERE ARE GONE, and neither has moved anywhere. `Cloud Type`
        // was a scalar between "flat sheet" and "heaped cloud" that fed one analytic curve, and
        // `Cloud Type Variance` mixed noise into it so that neighbouring clouds would not all reach the
        // same ceiling. Both are answered by the profile TABLE and answered better: the table's second
        // axis is the placement pattern's own value, so a cloud is low and flat at the rim of a patch and
        // a tower in its middle — height that is CORRELATED with how much cloud is there, instead of
        // height sprinkled by a noise that knew nothing about the patch it was decorating. A tower on a
        // thin edge was the visible cost of the old arrangement.
        //
        // A THIRD FIELD WAS BUILT HERE AND MEASURED AWAY. `Shape Distortion` drove Unreal's
        // height-dependent domain warp — the second noise that displaces the coverage field's coordinates
        // so that one flat pattern cannot be extruded upward into a column. Over the WHOLE travel of the
        // control it moved ImageStat contrast by at most 0.018 and with no consistent sign, against the
        // 0.08 the profile table itself moved and the 0.2 that separates two species; the six rows of
        // numbers are in Common/CloudField.glslh. It buys nothing HERE because our coverage field is
        // already sampled in full three dimensions, which is a different solution to the same problem —
        // and it cost one fetch of the noise volume per sample, in the march and in every light-march
        // sample beneath it. A control that moves nothing is what §1.3 of the contract calls a TODO
        // wearing a feature's clothes.

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

        // ---- Noise ----------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Noise Volume" ), Category( "Noise" ), Asset<CloudNoiseVolumeAsset>,
                  Tooltip( "The 3D noise the shape is built from — drag a .dcnv from the Content Browser or "
                           "bake one in Window > Cloud Noise Volume. Empty uses the engine's built-in "
                           "default. Two volumes with different seeds or periods give the same weather a "
                           "different EDGE, which is the whole reason this is an asset." ) )
        // THE SEEDS AND OCTAVE COUNTS THAT USED TO SIT HERE ARE GONE, and they are not hidden anywhere:
        // WeatherSeed, WeatherOctaves, DetailSeed and DetailOctaves described a GPU bake that no longer
        // exists. They were properties of what the volume CONTAINS, so they moved into the volume — the
        // container's header stores the seed, the four lattice periods and the curl strength, the panel
        // authors them, and the generator is the only thing that reads them. Left on the component they
        // would have been four sliders that rebake nothing, which is the exact shape of dead setting this
        // programme's contract forbids.
        Assets::AssetHandle NoiseVolume;

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

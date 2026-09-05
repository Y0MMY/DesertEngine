// Does every exposed setting actually reach something?
//
// A slider that moves nothing is the failure mode this programme was written to avoid, and it is not
// caught by any build: an unread field compiles, serializes, appears in Details and does nothing at all.
// So the reflected field list of EVERY reflected type is enumerated here and matched against an explicit
// table that says, for every single field, WHO consumes it.
//
// A row has exactly one of three kinds:
//
//   * WIRED   - names a source file that must contain an ANCHORED READ of the field (see
//               setting_consumers_reader.hpp). Delete the read and this suite goes red.
//   * PENDING - names the TASK that owes the field a consumer. The per-component counts at the bottom
//               pin how many exist, so a field joining a component without a reader is a reviewable
//               edit rather than a silent one.
//   * DEAD    - the field has NO consumer today, named one by one with the reason. This is the known
//               debt list: `kKnownDeadSettings` restates every one of them and a test pins the list
//               exactly, so neither a new dead setting nor the repair of an old one passes quietly.
//
// Every reflected field must appear in exactly one row, and every reflected TYPE must appear in the
// census, so a field or a type added tomorrow fails this suite until somebody decides which kind it is.
// That decision is the point.
//
// WHAT CHANGED IN Д23, AND WHY IT WAS THE WHOLE TASK. Two things were measured and both were bad.
//
//   1. COVERAGE. This census guarded FOUR of the engine's THIRTY-SEVEN reflected types (sky, fog,
//      clouds, hero clouds), later five. Twenty-one of the rest are the UI components - the ones with
//      the most fields and the least GPU, i.e. the ones where a dead setting is invisible. All 37 types
//      are covered now, and `EveryReflectedTypeIsUnderThisCensus` makes the 38th fail here first.
//   2. THE ROW SHAPE. A WIRED row used to assert that the named file mentions the field's NAME. `Sprite`
//      belongs to the canvas, the button, the panel AND the image, so when У3 deleted the canvas's own
//      background draw from UICanvasRenderer2D.cpp the row stayed green - proven by mutation, not by
//      argument. A row now asserts an anchored read: a member access of the field on a receiver that the
//      same file binds to THIS type. The mutation reddens it; renaming a local variable does not.

#include "setting_consumers_reader.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Reflection/ReflectionRegistry.hpp>
#include <Engine/Reflection/ReflectionTypes.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using Desert::Reflection::FieldInfo;
using Desert::Reflection::ReflectionRegistry;
using Desert::Reflection::TypeInfo;

namespace
{
    struct Row
    {
        const char* Field;

        // Exactly one of these is set. Where = a repo-relative source path that must contain an anchored
        // read; Task = the task that owes a consumer; Dead = why nothing reads it, and it must also be
        // named in kKnownDeadSettings below.
        const char* Where = nullptr;
        const char* Task  = nullptr;
        const char* Dead  = nullptr;
    };

    // How a consumer file is allowed to get hold of a value of this type. `Component` is the ECS wrapper
    // whose `Data` member holds it (NOT derivable from the type name - `DirectionalLightData` lives in
    // `DirectionLightComponent` and `UITextData` in `UITextComponent2D`, and a convention that guessed
    // would have silently found no receivers and reported ten live fields as dead). `Accessor` is a
    // getter that returns the type, which is how a non-component like SceneSettings is reached.
    struct Census
    {
        const char* Type;
        const char* Component;
        const char* Accessor;
        const Row*  Rows;
        std::size_t Count;
    };

    // ------------------------------------------------------------------------------------------------
    // Sky: every field is wired. The artistic-gradient group through the sky pass and the IBL bake, the
    // physical-atmosphere group through the LUT passes and the Phase 2 sky pass, and the
    // aerial-perspective group through the Phase 3 froxel volume and the atmospheric-fog pass.
    // ------------------------------------------------------------------------------------------------

    constexpr const char* kSkySettings = "Desert/Desert/Source/Engine/Graphic/SkySettings.hpp";
    constexpr const char* kTimeOfDay   = "Desert/Desert/Source/Engine/ECS/System/TimeOfDayECSSystem.hpp";
    constexpr const char* kCollector   = "Desert/Desert/Source/Engine/ECS/System/SkyboxECSSystem.hpp";
    constexpr const char* kSkyWidget =
         "Editor/Source/Editor/Panels/SceneProperties/ComponentWidgets/SkyAtmosphereComponent.cpp";

    constexpr Row kSkyRows[] = {
         // The collector decides whether the atmosphere drives the frame at all.
         { "Enabled", kCollector },

         // MakeSkySettings is the one funnel from component to render command; from there the palette
         // reaches the screen pass and the IBL bake through the same packed block.
         { "SkyBrightness", kSkySettings },
         { "HorizonFalloff", kSkySettings },
         { "ZenithColor", kSkySettings },
         { "HorizonColor", kSkySettings },
         { "GroundColor", kSkySettings },
         { "NightColor", kSkySettings },
         { "SunIntensity", kSkySettings },
         { "SunColor", kSkySettings },
         { "SunAngularDiameter", kSkySettings },
         { "SunGlow", kSkySettings },
         { "SunsetColor", kSkySettings },
         { "SunsetIntensity", kSkySettings },
         { "StarIntensity", kSkySettings },

         // The time-of-day driver turns these five into the sun's transform.
         { "DriveSunFromTimeOfDay", kTimeOfDay },
         { "TimeOfDay", kTimeOfDay },
         { "DayLengthSeconds", kTimeOfDay },
         { "Latitude", kTimeOfDay },
         { "NorthOffset", kTimeOfDay },

         // Environment-bake policy and size, carried in the same settings block.
         { "AutoRebakeEnvironment", kSkySettings },
         { "RebakeSunAngleThreshold", kSkySettings },
         { "EnvironmentResolution", kSkySettings },

         // Display-only state, and the widget is what maintains it.
         { "ActivePreset", kSkyWidget },

         // Converted to world units on the C++ side.
         { "PlanetRadius", kSkySettings },

         // ---- The physical atmosphere (Phase 0/1 of the Sky Atmosphere programme) ------------------
         // The medium group funnels through MakeSkySettings into the sky payload's medium block, where
         // the SkyTransmittanceLut / SkyMultiScatterLut compute passes read it — a fingerprint change
         // re-dispatches both, so each of these fields moves real GPU texels today.
         { "Model", kSkySettings }, // gates the LUT dispatch (SkyboxRenderer::ExecuteAtmosphereLuts)
         { "AtmosphereHeight", kSkySettings },
         { "MultiScatteringFactor", kSkySettings },
         { "GroundAlbedo", kSkySettings },
         { "RayleighScatteringScale", kSkySettings },
         { "RayleighScattering", kSkySettings },
         { "RayleighExponentialDistribution", kSkySettings },
         { "MieScatteringScale", kSkySettings },
         { "MieScattering", kSkySettings },
         { "MieAbsorptionScale", kSkySettings },
         { "MieAbsorption", kSkySettings },
         { "MieExponentialDistribution", kSkySettings },
         { "OtherAbsorptionScale", kSkySettings },
         { "OtherAbsorption", kSkySettings },
         { "AbsorptionTipAltitude", kSkySettings },
         { "AbsorptionTipValue", kSkySettings },
         { "AbsorptionTentWidth", kSkySettings },

         // Wired by Phase 2: MieAnisotropy is the Cornette-Shanks g of the scattering integrator
         // (Common/SkyScattering.glslh via the SkyViewLut / BakeProceduralSky marches); the two
         // art-direction tints funnel through MakeSkySettings into the payload's Phase 2 lanes, read
         // by the physical sky pass (SkyLuminanceFactor, on-screen pixels only) and inside every
         // scattering integration (SkyAndAerialPerspectiveLuminanceFactor).
         { "MieAnisotropy", kSkySettings },
         { "SkyLuminanceFactor", kSkySettings },
         { "SkyAndAerialPerspectiveLuminanceFactor", kSkySettings },

         // Wired by Phase 3: all three funnel through MakeSkySettings into SkySettings, from where
         // SkyboxRenderer fills the 32x32x16 aerial-perspective volume (start depth and distance, on
         // the fill's push block) and the atmospheric-fog pass reads it (distance and view-distance
         // scale, published on AtmosphereEnv). Every one of them moves real froxels today.
         { "AerialPerspectiveViewDistanceScale", kSkySettings },
         { "AerialPerspectiveStartDepth", kSkySettings },
         { "AerialPerspectiveDistance", kSkySettings },
    };

    // ------------------------------------------------------------------------------------------------
    // Height fog: the component and its pass shipped together (Sky plan Phase 5), so every field is
    // WIRED - nothing pending. One funnel consumes them: PackFogParams in FogPayload.hpp turns each
    // field into the GPU block the fog pass evaluates; Enabled is the renderer's own dispatch gate.
    // ------------------------------------------------------------------------------------------------

    constexpr const char* kFogPayload = "Desert/Desert/Source/Engine/Graphic/Fog/FogPayload.hpp";
    constexpr const char* kFogRenderer =
         "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Fog/HeightFogRenderer.cpp";

    constexpr Row kFogRows[] = {
         { "Enabled", kFogRenderer }, // the zero-cost gate: off means no allocation and no dispatch

         { "FogDensity", kFogPayload },
         { "FogHeightFalloff", kFogPayload },
         { "FogInscatteringLuminance", kFogPayload },
         { "SkyAtmosphereAmbientContributionColorScale", kFogPayload },
         { "FogMaxOpacity", kFogPayload },
         { "StartDistance", kFogPayload },
         { "FogCutoffDistance", kFogPayload },

         { "SecondFogDensity", kFogPayload },
         { "SecondFogHeightFalloff", kFogPayload },
         { "SecondFogHeightOffset", kFogPayload },

         { "DirectionalInscatteringExponent", kFogPayload },
         { "DirectionalInscatteringStartDistance", kFogPayload },
         { "DirectionalInscatteringLuminance", kFogPayload },
    };

    // ------------------------------------------------------------------------------------------------
    // Volumetric clouds: every field is WIRED, and there are three consumers rather than one because the
    // component's fields reach the GPU by three different routes.
    //
    //   * PackCloudParams turns the per-frame settings into the twelve-vec4 block the march reads. It
    //     lives in CloudPayload.hpp.
    //   * The ECS system owns the timestep, so the two wind fields are integrated there into the offset
    //     the packer is handed - the component carries no accumulated state of its own.
    //   * Enabled is the renderer's dispatch gate: off allocates nothing and dispatches nothing, which is
    //     a decision the packer cannot make because it runs after it.
    //
    // WHAT USED TO BE HERE AND IS NOT. Four rows - WeatherSeed, WeatherOctaves, DetailSeed and
    // DetailOctaves - pointed at a bake key that turned them into the push constant of a compute pass.
    // That pass is gone: the noise volume is an asset generated offline, its seed and lattice periods live
    // in the container's own header, and the component names the volume instead of describing how to bake
    // one. Four rows removed rather than repointed, because there is nothing left for them to point at.
    //
    // And four more since: LayerBottomAltitude and LayerThickness stated the shell by hand, which the
    // cloud type now states in kilometres and the packer computes; the old scalar CloudType and its
    // variance drove one analytic profile curve, which is now a per-type AUTHORED curve living in the
    // type's own asset. Every one of the four was removed rather than repointed.
    //
    // And ONE more with T1: NoiseVolume. It was not removed - it MOVED, onto the cloud type asset, because
    // the character of a cloud's edge is a property of the kind of cloud rather than of the layer's
    // weather. There is no row for it here because it is no longer a field of this component; the row that
    // replaced it is CloudType, and the renderer is what resolves that handle into both the type's numbers
    // and the volume.
    // ------------------------------------------------------------------------------------------------

    constexpr const char* kCloudPayload = "Desert/Desert/Source/Engine/Graphic/Clouds/CloudPayload.hpp";
    constexpr const char* kCloudSystem  = "Desert/Desert/Source/Engine/ECS/System/VolumetricCloudECSSystem.hpp";
    constexpr const char* kCloudRenderer =
         "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Clouds/VolumetricCloudRenderer.cpp";

    constexpr Row kCloudRows[] = {
         { "Enabled", kCloudRenderer }, // the zero-cost gate: off means no allocation and no dispatch

         // Cloud Layer - the shell the march intersects. The CLOUD TYPES are what the shell is built
         // from, and unlike every other row here they are resolved rather than packed: the renderer turns
         // each handle into thirteen numbers and a vertical profile curve through Runtime::CloudTypeService,
         // and those become
         // Layer.y/Layer.z (the shell, the UNION of the set's bands) and one entry each of SpeciesEdge
         // and SpeciesPlacement. Two fields that used to state the shell by hand are gone, because an
         // authored shell and a type's altitudes are two numbers obliged to agree.
         //
         // FOUR ROWS AND NOT ONE SINCE T3: a layer carries a SET of kinds of cloud. Every one of them has
         // to have a consumer or it is a slot an artist can fill and never see - which is the exact shape
         // of the defect this whole suite exists to catch, and the easiest one to introduce by wiring only
         // the first slot.
         { "CloudType1", kCloudRenderer },
         { "CloudType2", kCloudRenderer },
         { "CloudType3", kCloudRenderer },
         { "CloudType4", kCloudRenderer },
         { "PlanetRadius", kCloudPayload },
         { "MaxViewDistance", kCloudPayload },
         { "TracingStartDistance", kCloudPayload },
         { "TracingStartMaxDistance", kCloudPayload },

         // Weather - THE BAKE, and not the payload. Phase E5 moved the four numbers that decide WHERE
         // cloud is out of the parameter block: the lumps of the modelling volume are placed on a lattice
         // on the CPU, once, when the settings change or the region shifts, so what reads them is
         // VolumetricCloudRenderer::BuildProceduralParams and what the march is handed instead is where
         // the volume is. A row still pointing at CloudPayload.hpp would have been this suite failing for
         // the right reason - the read WAS removed - and it did.
         { "Coverage", kCloudRenderer },
         { "CoverageContrast", kCloudRenderer },

         // WEATHER TILE SIZE IS READ IN THE COMPONENT'S OWN HEADER NOW, and this suite is what noticed.
         // `ECS::CloudLayerLatticeKm` states "four cells to a tile" ONCE, because a second reader appeared
         // — the Cloud Layout panel measures a painting's strokes against the cell — and the renderer calls
         // it instead of spelling the ratio out a second time. The field's last textual mention in
         // VolumetricCloudRenderer.cpp went with it, and the row pointing there went red. That is this
         // suite failing for exactly the reason it exists: the named consumer stopped reading the field.
         { "WeatherTileSize", "Desert/Desert/Source/Engine/ECS/VolumetricCloudComponent.hpp" },

         { "RegionSize", kCloudRenderer },
         { "Seed", kCloudRenderer },

         // Placement - where the clouds are, which is baked and not marched. Same consumer as the four
         // rows above it and for the same reason: VolumetricCloudRenderer::BuildProceduralParams turns
         // them into CloudProceduralFieldParams and the bake reads them there.
         { "PlacementDensity", kCloudRenderer },
         { "PlacementScatter", kCloudRenderer },
         { "PlacementSizeVariety", kCloudRenderer },
         { "PatchTileSize", kCloudRenderer },
         { "PatchStrength", kCloudRenderer },

         // Layout - the PAINTED sky, and the same consumer as Placement for the same reason: the painting
         // decides which lattice cells carry a cloud, which is a bake question and not a march one.
         // `CloudLayout` is a handle the renderer resolves through Runtime::CloudLayoutService and hands to
         // the bake as bytes; the other five are numbers that travel in CloudProceduralFieldParams.
         //
         // THE ROW FOR `CloudLayout` IS THE ONE WORTH LOOKING AT TWICE. An asset slot that resolves to
         // nothing renders exactly like an asset slot that is not wired, so this is precisely the shape of
         // dead setting this suite exists to catch — and unlike a slider, nobody notices, because the sky
         // an unwired painting produces is a perfectly good procedural sky.
         { "CloudLayout", kCloudRenderer },
         { "LayoutPatternStrength", kCloudRenderer },
         { "LayoutMaskStrength", kCloudRenderer },
         { "LayoutRepeats", kCloudRenderer },
         { "LayoutRotation", kCloudRenderer },
         { "LayoutOffset", kCloudRenderer },

         // Detail - the erosion field.
         { "DetailTileSize", kCloudPayload },
         { "DetailStrength", kCloudPayload },
         { "DensityScale", kCloudPayload },
         { "NearFadeStartDistance", kCloudPayload },
         { "NearFadeEndDistance", kCloudPayload },
         { "ExtinctionScale", kCloudPayload },

         // Lighting.
         { "ScatteringAlbedo", kCloudPayload },
         { "PhaseG", kCloudPayload },
         { "PhaseGBackward", kCloudPayload },
         { "PhaseBlend", kCloudPayload },
         { "AmbientOcclusionStrength", kCloudPayload },
         // THE RENDERER AND NOT THE PAYLOAD, and the difference is the point: this field never reaches the
         // parameter block at all. It decides whether ExecuteInFrame dispatches the sky-light occlusion
         // volume, and what the march is told is only whether that dispatch HAPPENED — a property of the
         // frame rather than of the weather, on CloudPush::Frame.
         { "SkyOcclusionVolume", kCloudRenderer },
         // THE PAYLOAD AND NOT THE RENDERER, which is the opposite of the row above and for a reason worth
         // stating: this field changes WHAT THE PARAMETER BLOCK MEANS. With it on, SunColour.rgb is the
         // sun's outer-space illuminance and both marches of the field apply the atmosphere themselves;
         // with it off, the same field is the ground-level product. The renderer reads the field too — it
         // raises CloudPush::Frame.y, binds the LUT, and tells the environment bake — but it does so
         // through the packer's own CloudUsesPerSampleSunTransmittance, so the packer is where the
         // decision lives.
         { "PerSampleAtmosphereTransmittance", kCloudPayload },
         { "AerialPerspectiveStartDistance", kCloudPayload },
         { "AerialPerspectiveFadeDistance", kCloudPayload },
         { "LightMarchDistance", kCloudPayload },
         { "LightMarchSamples", kCloudPayload },
         { "MultiScatterOctaves", kCloudPayload },
         { "MultiScatterContribution", kCloudPayload },
         { "MultiScatterOcclusion", kCloudPayload },
         { "MultiScatterEccentricity", kCloudPayload },
         { "AmbientScale", kCloudPayload },

         // Shadows on the world. NEITHER GOES THROUGH THE PACKER, and that is the one thing worth
         // knowing about this pair: the shadow map is not part of CloudGpuPayload at all. `CastShadows`
         // is the zero-cost gate the renderer tests before it allocates or dispatches anything, and
         // `ShadowStrength` reaches the GPU through the CONSUMER — CloudShadowUniforms::Params.w in
         // MaterialDeferredLighting — because the map holds the medium's own physical numbers and the
         // artist's dial is applied where the transmittance is reconstructed. Both are read by
         // VolumetricCloudRenderer::GetShadowStrength(), which is the one place the two are combined.
         { "CastShadows", kCloudRenderer },
         { "ShadowStrength", kCloudRenderer },

         // Quality.
         { "MaxSteps", kCloudPayload },
         { "StopTransmittance", kCloudPayload },

         // Animation - integrated against the timestep by the system that owns it, and handed to the
         // packer as an offset.
         { "WindDirection", kCloudSystem },
         { "WindSpeed", kCloudSystem },
    };

    // ------------------------------------------------------------------------------------------------
    // The HERO CLOUD - slot A of the seam, one sculpted body placed by an entity's own transform.
    //
    // ITS FIELDS SPLIT THREE WAYS AND EACH WAY MEANS SOMETHING. `Enabled` and `Volume` are the ECS
    // system's and the renderer's: the first decides whether the instance is COLLECTED at all (which is
    // what makes a disabled hero cloud cost nothing rather than nearly nothing), the second is a handle
    // the renderer resolves through Runtime::CloudModellingService. Everything else is packed, and the
    // packer is its own file rather than CloudPayload.hpp because a hero cloud is a per-frame LIST and
    // the layer is one block.
    //
    // There is no row for a transform here, and that is the point of the component's shape: WHERE the
    // cloud is comes from the entity, so there is no authored position to leave unread.
    // ------------------------------------------------------------------------------------------------

    constexpr const char* kHeroPayload = "Desert/Desert/Source/Engine/Graphic/Clouds/CloudAuthoredPayload.hpp";

    constexpr Row kHeroCloudRows[] = {
         { "Enabled", kCloudSystem }, // the zero-cost gate: not collected, so the march's loop is empty
         { "Volume", kCloudSystem },  // resolved through the modelling service, never packed as a number
         { "Strength", kHeroPayload },         { "SuppressProceduralField", kHeroPayload },
         { "DetailFactor", kHeroPayload },     { "DensityFactor", kHeroPayload },
         { "ExtinctionFactor", kHeroPayload },
    };

    // ------------------------------------------------------------------------------------------------
    // The UI canvas. This table is here because of what it caught by NOT being here.
    //
    // UICanvasData::Sprite - "Background Sprite" - was reflected, serialized, shown in Details and read by
    // NOTHING for its whole life. The old ImGui canvas renderer drew it only when handed a SpriteResolver,
    // and the panel that owned it never passed one; then that renderer was deleted and the field had no
    // reader at all. It is fixed (UICanvasRenderer2D draws the full-canvas backdrop before the children),
    // and this table is what keeps a tenth field from joining the component the same way.
    //
    // AND THE ROW SHAPE ALONE WOULD NOT HAVE CAUGHT IT EITHER, which is what Д23 fixed one level up:
    // "Sprite" is also a field of the button, the panel and the image, so UICanvasRenderer2D.cpp mentioned
    // the word on the day the canvas's copy was dead. The rows below now demand an anchored read, and the
    // canvas's background additionally keeps the exact-expression assertion further down the file.
    // ------------------------------------------------------------------------------------------------

    constexpr const char* kCanvasRenderer = "Desert/Desert/Source/Engine/UI/UICanvasRenderer2D.cpp";
    constexpr const char* kCanvasLayout   = "Desert/Desert/Source/Engine/UI/UICanvasLayout.cpp";

    constexpr Row kCanvasRows[] = {
         // The canvas rect and its scale: ResolveCanvas, at the top of the walk.
         { "ScaleMode", kCanvasRenderer },
         { "ReferenceWidth", kCanvasRenderer },
         { "ReferenceHeight", kCanvasRenderer },
         { "MatchWidthHeight", kCanvasRenderer },
         // Screen-space vs billboarded, and the distance scale the billboard uses.
         { "RenderMode", kCanvasRenderer },
         { "WorldScale", kCanvasRenderer },
         // The gate, the backdrop and the notch inset.
         { "Visible", kCanvasRenderer },
         { "Sprite", kCanvasRenderer },
         { "SafeArea", kCanvasRenderer },
    };

    // ------------------------------------------------------------------------------------------------
    // SCENE SETTINGS - the one reflected type that is not an ECS component, so it is reached through an
    // accessor (`GetSettings()`) rather than through a wrapper's `Data`.
    //
    // Most of it funnels through SceneRenderer::SetSceneData, which is where the scene's authored
    // rendering policy becomes the frame's. The exceptions are named individually because WHERE a setting
    // is consumed is the interesting half of the row: the two editor-only aids are read by the editor
    // passes they gate (and by nothing in the engine, deliberately), physics reads its own two, and the
    // splash trio is the RUNTIME's - it is the only thing in this file consumed by the shipping player
    // and by neither the editor nor the renderer.
    // ------------------------------------------------------------------------------------------------

    constexpr const char* kSceneRenderer = "Desert/Desert/Source/Engine/Graphic/SceneRenderer.cpp";
    constexpr const char* kPhysicsSystem = "Desert/Desert/Source/Engine/ECS/System/PhysicsECSSystem.hpp";
    constexpr const char* kGridPass      = "Editor/Source/Editor/RenderSystems/Passes/EditorGridPass.cpp";
    constexpr const char* kColliderPass  = "Editor/Source/Editor/RenderSystems/Passes/EditorColliderPass.cpp";
    constexpr const char* kRuntimeLayer  = "Runtime/Source/RuntimeLayer.cpp";

    constexpr Row kSceneSettingsRows[] = {
         { "RenderingPath", kSceneRenderer },
         { "DeferredDebug", kSceneRenderer },
         { "EnableSSAO", kSceneRenderer },
         { "CloudQualityTier", kSceneRenderer },
         { "GlobalIllumination", kSceneRenderer },
         { "GIIntensity", kSceneRenderer },
         { "EnableSSR", kSceneRenderer },
         { "SSRIntensity", kSceneRenderer },
         { "SSRMaxDistance", kSceneRenderer },
         { "EnableShadows", kSceneRenderer },
         { "ShadowBias", kSceneRenderer },
         { "CascadeSplitLambda", kSceneRenderer },
         { "ShadowDebug", kSceneRenderer },
         { "Tonemapper", kSceneRenderer },
         { "Exposure", kSceneRenderer },
         { "Gamma", kSceneRenderer },
         { "WhitePoint", kSceneRenderer },
         { "AutoExposure", kSceneRenderer },
         { "AutoExposureKey", kSceneRenderer },
         { "AutoExposureSpeed", kSceneRenderer },
         { "AutoExposureMin", kSceneRenderer },
         { "AutoExposureMax", kSceneRenderer },
         { "AA", kSceneRenderer },
         { "EnableBloom", kSceneRenderer },
         { "BloomThreshold", kSceneRenderer },
         { "BloomIntensity", kSceneRenderer },
         { "LensDispersion", kSceneRenderer },
         { "EnableLensFlare", kSceneRenderer },
         { "LensFlareIntensity", kSceneRenderer },
         { "LensFlareTint", kSceneRenderer },
         { "LensFlareThreshold", kSceneRenderer },
         { "LensFlareGhostCount", kSceneRenderer },
         { "LensFlareGhostSpacing", kSceneRenderer },
         { "LensFlareGhostSizeNear", kSceneRenderer },
         { "LensFlareGhostSizeFar", kSceneRenderer },
         { "LensFlareGhostTintInner", kSceneRenderer },
         { "LensFlareGhostTintOuter", kSceneRenderer },
         { "LensFlareHaloIntensity", kSceneRenderer },
         { "LensFlareHaloRadius", kSceneRenderer },
         { "LensFlareStreakIntensity", kSceneRenderer },
         { "LensFlareStreakLength", kSceneRenderer },
         { "LensFlareStreakAngle", kSceneRenderer },
         { "LensFlareChromaShift", kSceneRenderer },
         { "TextureFilterMode", kSceneRenderer },
         { "Anisotropy", kSceneRenderer },

         // Editor aids. The pass that draws the thing is what reads the flag, which is why these two
         // point at Editor/ and the debug-view trio above them does not.
         { "ShowGrid", kGridPass },
         { "ShowColliders", kColliderPass },
         { "ShowBoundingBoxes", kSceneRenderer },
         { "BoundingBoxColor", kSceneRenderer },
         { "BoundingBoxLineWidth", kSceneRenderer },
         { "WireframeMode", kSceneRenderer },
         { "MeshLOD", kSceneRenderer },

         { "Gravity", kPhysicsSystem },
         // DEAD. Nothing anywhere reads it - see kKnownDeadSettings.
         { "PauseSimulation", nullptr, nullptr, "no reader anywhere; the editor's own pause is a separate "
                                                "editor-side state and never consults this field" },

         { "WindDirection", kSceneRenderer },
         { "WindStrength", kSceneRenderer },
         { "WindTurbulence", kSceneRenderer },

         // The shipping player's, and nothing else's.
         { "SplashSprite", kRuntimeLayer },
         { "SplashDuration", kRuntimeLayer },
         { "SplashFade", kRuntimeLayer },
    };

    // ------------------------------------------------------------------------------------------------
    // The scene's own components: camera, terrain, the three lights, particles, skybox, physics, audio.
    //
    // THE DIRECTIONAL LIGHT IS THE ONE TO READ TWICE. Its ten fields split between two consumers and the
    // split is physical: Colour and Intensity are surface illumination, collected with every other light
    // by Scene::CollectRenderData; the eight Atmosphere/Light-Shafts fields belong to whichever light the
    // sky elected as its sun, so the SKY's collector is what reads them, and reading them anywhere else
    // would let an unmarked light in a corner of the scene tint the sky.
    // ------------------------------------------------------------------------------------------------

    constexpr const char* kScene       = "Desert/Desert/Source/Engine/Core/Scene.cpp";
    constexpr const char* kTerrain     = "Desert/Desert/Source/Engine/ECS/System/TerrainECSSystem.hpp";
    constexpr const char* kPointLight  = "Desert/Desert/Source/Engine/ECS/System/PointLightSystem.hpp";
    constexpr const char* kSpotLight   = "Desert/Desert/Source/Engine/ECS/System/SpotLightSystem.hpp";
    constexpr const char* kLightGizmo  = "Editor/Source/Editor/Panels/ViewportPanel/LightGizmoRenderer.cpp";
    constexpr const char* kParticles   = "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Particles/ParticleRenderer.cpp";
    constexpr const char* kAudioSystem = "Desert/Desert/Source/Engine/ECS/System/AudioECSSystem.hpp";

    constexpr Row kCameraRows[] = {
         { "IsMainCamera", kScene }, // which camera the scene renders through
         { "FOV", kScene },
         { "Near", kScene },
         { "Far", kScene },
    };

    constexpr Row kTerrainRows[] = {
         { "Material", kTerrain },   { "Size", kTerrain },
         { "Resolution", kTerrain },  { "HeightScale", kTerrain },
         { "NoiseFrequency", kTerrain }, { "Seed", kTerrain },
         { "GrassMode", kTerrain },  { "RockMode", kTerrain },
         { "SnowMode", kTerrain },   { "EnableGrass", kTerrain },
         { "GrassDensity", kTerrain }, { "GrassHeight", kTerrain },
         { "GrassBladesPerClump", kTerrain }, { "GrassWidth", kTerrain },
         { "GrassBrightness", kTerrain },
    };

    constexpr Row kDirLightRows[] = {
         { "Color", kScene },
         { "Intensity", kScene },
         { "AtmosphereSunLight", kCollector },
         { "AtmosphereSunLightIndex", kCollector },
         { "AffectedByAtmosphereTransmittance", kCollector },
         { "LightShaftBloom", kCollector },
         { "BloomScale", kCollector },
         { "BloomThreshold", kCollector },
         { "BloomMaxBrightness", kCollector },
         { "BloomTint", kCollector },
    };

    constexpr Row kPointLightRows[] = {
         { "Color", kPointLight },     { "Intensity", kPointLight },
         { "Radius", kPointLight },    { "MinRadius", kPointLight },
         { "Falloff", kPointLight },
         // An EDITOR-ONLY visualisation flag, deliberately: it draws the radius gizmo and reaches no
         // shader at all, so the gizmo renderer is the honest consumer to name.
         { "ShowRadius", kLightGizmo },
    };

    constexpr Row kSpotLightRows[] = {
         { "Color", kSpotLight },          { "Intensity", kSpotLight },
         { "Range", kSpotLight },          { "InnerConeAngle", kSpotLight },
         { "OuterConeAngle", kSpotLight }, { "Falloff", kSpotLight },
         { "ShowCone", kLightGizmo }, // editor-only, same as ShowRadius above
    };

    constexpr Row kParticleRows[] = {
         { "Enabled", kParticles },
         { "MaxParticles", kParticles },
         { "SpawnRate", kParticles },
         { "Looping", kParticles },
         // DEAD. Nothing reads it, in C++ or in any shader - see kKnownDeadSettings.
         { "WorldSpace", nullptr, nullptr,
           "no reader in C++ or in any particle shader; the simulation is unconditionally world-space" },
         { "Lifetime", kParticles },
         { "LifetimeVariance", kParticles },
         { "StartSpeed", kParticles },
         { "SpeedVariance", kParticles },
         { "Direction", kParticles },
         { "ConeAngle", kParticles },
         { "Gravity", kParticles },
         { "StartSize", kParticles },
         { "SizeCurvePower", kParticles },
         { "EndSize", kParticles },
         { "StartColor", kParticles },
         { "EndColor", kParticles },
         { "StartAlpha", kParticles },
         { "EndAlpha", kParticles },
         { "Blend", kParticles },
    };

    constexpr Row kSkyboxRows[] = {
         { "SkyboxHandle", kCollector },
         { "Intensity", kCollector },
    };

    constexpr Row kColliderRows[] = {
         { "Shape", kPhysicsSystem },  { "HalfExtents", kPhysicsSystem },
         { "Radius", kPhysicsSystem }, { "HalfHeight", kPhysicsSystem },
    };

    constexpr Row kRigidBodyRows[] = {
         { "Type", kPhysicsSystem },     { "Mass", kPhysicsSystem },
         { "Friction", kPhysicsSystem }, { "Restitution", kPhysicsSystem },
    };

    constexpr Row kCharacterControllerRows[] = {
         { "Radius", kPhysicsSystem },      { "Height", kPhysicsSystem },
         { "MaxSlopeDeg", kPhysicsSystem }, { "Gravity", kPhysicsSystem },
    };

    constexpr Row kAudioRows[] = {
         { "Clip", kAudioSystem },     { "Volume", kAudioSystem }, { "Loop", kAudioSystem },
         { "AutoPlay", kAudioSystem }, { "Spatial", kAudioSystem },
    };

    // ------------------------------------------------------------------------------------------------
    // THE TWENTY UI COMPONENTS THAT HAD NO TABLE. This is the bulk of what Д23 added, and it is the half
    // of the engine where a dead setting is hardest to see: a UI field that nothing reads produces a
    // perfectly ordinary-looking panel, exactly as the canvas's own background did for its whole life.
    //
    // Two consumers between them. UICanvasLayout.cpp answers WHERE an element is - anchors, offsets,
    // fitters, the layout groups and the two inherited axes; UICanvasRenderer2D.cpp answers WHAT is drawn
    // and what the pointer hits. A field's row names whichever of the two actually reads it, and the
    // split is not cosmetic: `Visibility` is read by the layout walk (a collapsed element gives its slot
    // back) while `HitTest` is read by the hit walk, and swapping them would name a file that never sees
    // the value.
    // ------------------------------------------------------------------------------------------------

    constexpr Row kLayoutRows[] = {
         { "AnchorMin", kCanvasLayout },
         { "AnchorMax", kCanvasLayout },
         { "OffsetMin", kCanvasLayout },
         { "OffsetMax", kCanvasLayout },
         { "CustomMinimumSize", kCanvasLayout },
         // DEAD. Reflected, serialized, drawn in Details, and read by nothing - see kKnownDeadSettings.
         { "Pivot", nullptr, nullptr,
           "no reader; the rect is resolved from anchors and offsets alone, and nothing rotates or scales "
           "an element about a pivot" },
         { "ClipContents", kCanvasRenderer },
         { "Visibility", kCanvasLayout },
         { "HitTest", kCanvasRenderer },
         { "AspectRatio", kCanvasLayout },
         { "AspectMode", kCanvasLayout },
         { "FlexGrow", kCanvasLayout },
         { "FitWidth", kCanvasLayout },
         { "FitHeight", kCanvasLayout },
    };

    constexpr Row kLayoutGroupRows[] = {
         { "Type", kCanvasLayout },     { "Padding", kCanvasLayout }, { "Spacing", kCanvasLayout },
         { "StretchCross", kCanvasLayout }, { "CellSize", kCanvasLayout }, { "Columns", kCanvasLayout },
    };

    constexpr Row kPanelRows[] = {
         { "Color", kCanvasRenderer },        { "Opacity", kCanvasRenderer },
         { "CornerRadius", kCanvasRenderer }, { "BackdropBlur", kCanvasRenderer },
         { "Sprite", kCanvasRenderer },       { "SpriteBorder", kCanvasRenderer },
         { "Video", kCanvasRenderer },        { "Circle", kCanvasRenderer },
         { "RingWidth", kCanvasRenderer },    { "RingColorA", kCanvasRenderer },
         { "RingColorB", kCanvasRenderer },   { "Pulse", kCanvasRenderer },
         { "PulseSpeed", kCanvasRenderer },   { "PulseMin", kCanvasRenderer },
         { "UseGradient", kCanvasRenderer },  { "GradientColor", kCanvasRenderer },
         { "BorderWidth", kCanvasRenderer },  { "BorderColor", kCanvasRenderer },
         { "Shadow", kCanvasRenderer },       { "ShadowColor", kCanvasRenderer },
         { "ShadowOffset", kCanvasRenderer }, { "Glow", kCanvasRenderer },
         { "GlowColor", kCanvasRenderer },    { "GlowSize", kCanvasRenderer },
    };

    constexpr Row kButtonRows[] = {
         { "NormalColor", kCanvasRenderer },   { "HoverColor", kCanvasRenderer },
         { "PressedColor", kCanvasRenderer },  { "Action", kCanvasRenderer },
         { "OnClickMessage", kCanvasRenderer }, { "Sprite", kCanvasRenderer },
         { "HoverSprite", kCanvasRenderer },   { "PressedSprite", kCanvasRenderer },
         { "SpriteBorder", kCanvasRenderer },  { "Selected", kCanvasRenderer },
         { "SelectedColor", kCanvasRenderer }, { "SelectedAccent", kCanvasRenderer },
         { "Disabled", kCanvasRenderer },      { "DisabledColor", kCanvasRenderer },
    };

    constexpr Row kTextRows[] = {
         { "Text", kCanvasRenderer },         { "FontSize", kCanvasRenderer },
         { "Font", kCanvasRenderer },         { "Color", kCanvasRenderer },
         { "Align", kCanvasRenderer },        { "VerticalAlign", kCanvasRenderer },
         { "Wrap", kCanvasRenderer },         { "LineSpacing", kCanvasRenderer },
         { "AutoSize", kCanvasRenderer },     { "MinFontSize", kCanvasRenderer },
         { "Overflow", kCanvasRenderer },     { "RichText", kCanvasRenderer },
         { "Marquee", kCanvasRenderer },      { "MarqueeSpeed", kCanvasRenderer },
         { "Shadow", kCanvasRenderer },       { "ShadowColor", kCanvasRenderer },
         { "ShadowOffset", kCanvasRenderer }, { "Outline", kCanvasRenderer },
         { "OutlineColor", kCanvasRenderer },
    };

    constexpr Row kImageRows[] = {
         { "Sprite", kCanvasRenderer }, { "Tint", kCanvasRenderer },
         { "Opacity", kCanvasRenderer }, { "SpriteBorder", kCanvasRenderer },
    };

    constexpr Row kIconRows[] = {
         { "Icon", kCanvasRenderer }, { "Color", kCanvasRenderer }, { "Scale", kCanvasRenderer },
    };

    constexpr Row kProgressBarRows[] = {
         { "Value", kCanvasRenderer }, { "Background", kCanvasRenderer },
         { "Fill", kCanvasRenderer },  { "CornerRadius", kCanvasRenderer },
    };

    constexpr Row kToggleRows[] = {
         { "Value", kCanvasRenderer },      { "BoxColor", kCanvasRenderer },
         { "CheckColor", kCanvasRenderer }, { "CornerRadius", kCanvasRenderer },
    };

    constexpr Row kSliderRows[] = {
         { "Value", kCanvasRenderer },      { "MinValue", kCanvasRenderer },
         { "MaxValue", kCanvasRenderer },   { "TrackColor", kCanvasRenderer },
         { "FillColor", kCanvasRenderer },  { "HandleColor", kCanvasRenderer },
    };

    constexpr Row kScrollViewRows[] = {
         { "ScrollY", kCanvasRenderer },       { "ContentHeight", kCanvasRenderer },
         { "Background", kCanvasRenderer },    { "ShowScrollbar", kCanvasRenderer },
         { "ScrollbarColor", kCanvasRenderer },
    };

    constexpr Row kInputFieldRows[] = {
         { "Text", kCanvasRenderer },             { "Placeholder", kCanvasRenderer },
         { "FontSize", kCanvasRenderer },         { "TextColor", kCanvasRenderer },
         { "PlaceholderColor", kCanvasRenderer }, { "Background", kCanvasRenderer },
         { "FocusColor", kCanvasRenderer },       { "CornerRadius", kCanvasRenderer },
    };

    constexpr Row kDropdownRows[] = {
         { "Options", kCanvasRenderer },   { "SelectedIndex", kCanvasRenderer },
         { "Open", kCanvasRenderer },      { "FontSize", kCanvasRenderer },
         { "Background", kCanvasRenderer }, { "TextColor", kCanvasRenderer },
         { "Highlight", kCanvasRenderer }, { "CornerRadius", kCanvasRenderer },
    };

    constexpr Row kTweenRows[] = {
         { "Property", kCanvasRenderer }, { "From", kCanvasRenderer },    { "To", kCanvasRenderer },
         { "Duration", kCanvasRenderer }, { "Delay", kCanvasRenderer },   { "Easing", kCanvasRenderer },
         { "Loop", kCanvasRenderer },     { "Playing", kCanvasRenderer }, { "RewindOnHide", kCanvasRenderer },
    };

    constexpr Row kBindingRows[] = {
         { "Key", kCanvasRenderer }, { "Target", kCanvasRenderer }, { "Format", kCanvasRenderer },
    };

    constexpr Row kScreenRows[] = {
         { "Name", kCanvasRenderer },
    };

    constexpr Row kScreenStackRows[] = {
         { "InitialScreen", kCanvasRenderer }, { "TransitionTime", kCanvasRenderer },
         { "SlidePx", kCanvasRenderer },       { "Easing", kCanvasRenderer },
    };

    constexpr Row kPointerEventsRows[] = {
         { "OnEnterMessage", kCanvasRenderer }, { "OnExitMessage", kCanvasRenderer },
         { "OnDownMessage", kCanvasRenderer },  { "OnUpMessage", kCanvasRenderer },
    };

    constexpr Row kDraggableRows[] = {
         { "Payload", kCanvasRenderer }, { "GhostOpacity", kCanvasRenderer },
    };

    constexpr Row kDropTargetRows[] = {
         { "Accepts", kCanvasRenderer }, { "OnDropMessage", kCanvasRenderer },
         { "HighlightColor", kCanvasRenderer },
    };

    // ------------------------------------------------------------------------------------------------
    // THE CENSUS. Thirty-seven reflected types, thirty-seven entries; adding a thirty-eighth fails
    // `EveryReflectedTypeIsUnderThisCensus` before it can reach a Details panel with no reader.
    // ------------------------------------------------------------------------------------------------

#define CENSUS_ROWS( rows ) ( rows ), std::size( rows )

    constexpr Census kCensus[] = {
         { "SceneSettings", nullptr, "GetSettings", CENSUS_ROWS( kSceneSettingsRows ) },
         { "SkyAtmosphereData", "SkyAtmosphereComponent", nullptr, CENSUS_ROWS( kSkyRows ) },
         { "ExponentialHeightFogData", "ExponentialHeightFogComponent", nullptr, CENSUS_ROWS( kFogRows ) },
         { "VolumetricCloudData", "VolumetricCloudComponent", nullptr, CENSUS_ROWS( kCloudRows ) },
         { "HeroCloudData", "HeroCloudComponent", nullptr, CENSUS_ROWS( kHeroCloudRows ) },

         { "CameraData", "CameraComponent", nullptr, CENSUS_ROWS( kCameraRows ) },
         { "TerrainData", "TerrainComponent", nullptr, CENSUS_ROWS( kTerrainRows ) },
         // NOT `DirectionalLightComponent`. The wrapper dropped the "al", and a census that guessed the
         // spelling would have found no receivers at all and called ten live fields dead.
         { "DirectionalLightData", "DirectionLightComponent", nullptr, CENSUS_ROWS( kDirLightRows ) },
         { "PointLightData", "PointLightComponent", nullptr, CENSUS_ROWS( kPointLightRows ) },
         { "SpotLightData", "SpotLightComponent", nullptr, CENSUS_ROWS( kSpotLightRows ) },
         { "ParticleEmitterData", "ParticleEmitterComponent", nullptr, CENSUS_ROWS( kParticleRows ) },
         // The one component whose reflected type IS the component: there is no `Data` member to hop
         // through, so it anchors on its own name.
         { "SkyboxComponent", nullptr, nullptr, CENSUS_ROWS( kSkyboxRows ) },
         { "ColliderData", "ColliderComponent", nullptr, CENSUS_ROWS( kColliderRows ) },
         { "RigidBodyData", "RigidBodyComponent", nullptr, CENSUS_ROWS( kRigidBodyRows ) },
         { "CharacterControllerData", "CharacterControllerComponent", nullptr,
           CENSUS_ROWS( kCharacterControllerRows ) },
         { "AudioSourceData", "AudioSourceComponent", nullptr, CENSUS_ROWS( kAudioRows ) },

         { "UICanvasData", "UICanvasComponent", nullptr, CENSUS_ROWS( kCanvasRows ) },
         { "UILayoutData", "UILayoutComponent", nullptr, CENSUS_ROWS( kLayoutRows ) },
         { "UILayoutGroupData", "UILayoutGroupComponent", nullptr, CENSUS_ROWS( kLayoutGroupRows ) },
         { "UIPanelData", "UIPanelComponent", nullptr, CENSUS_ROWS( kPanelRows ) },
         { "UIButtonData", "UIButtonComponent", nullptr, CENSUS_ROWS( kButtonRows ) },
         // ...and the other exception to the wrapper's spelling: the 2D suffix.
         { "UITextData", "UITextComponent2D", nullptr, CENSUS_ROWS( kTextRows ) },
         { "UIImageData", "UIImageComponent", nullptr, CENSUS_ROWS( kImageRows ) },
         { "UIIconData", "UIIconComponent", nullptr, CENSUS_ROWS( kIconRows ) },
         { "UIProgressBarData", "UIProgressBarComponent", nullptr, CENSUS_ROWS( kProgressBarRows ) },
         { "UIToggleData", "UIToggleComponent", nullptr, CENSUS_ROWS( kToggleRows ) },
         { "UISliderData", "UISliderComponent", nullptr, CENSUS_ROWS( kSliderRows ) },
         { "UIScrollViewData", "UIScrollViewComponent", nullptr, CENSUS_ROWS( kScrollViewRows ) },
         { "UIInputFieldData", "UIInputFieldComponent", nullptr, CENSUS_ROWS( kInputFieldRows ) },
         { "UIDropdownData", "UIDropdownComponent", nullptr, CENSUS_ROWS( kDropdownRows ) },
         { "UITweenData", "UITweenComponent", nullptr, CENSUS_ROWS( kTweenRows ) },
         { "UIBindingData", "UIBindingComponent", nullptr, CENSUS_ROWS( kBindingRows ) },
         { "UIScreenData", "UIScreenComponent", nullptr, CENSUS_ROWS( kScreenRows ) },
         { "UIScreenStackData", "UIScreenStackComponent", nullptr, CENSUS_ROWS( kScreenStackRows ) },
         { "UIPointerEventsData", "UIPointerEventsComponent", nullptr, CENSUS_ROWS( kPointerEventsRows ) },
         { "UIDraggableData", "UIDraggableComponent", nullptr, CENSUS_ROWS( kDraggableRows ) },
         { "UIDropTargetData", "UIDropTargetComponent", nullptr, CENSUS_ROWS( kDropTargetRows ) },
    };

#undef CENSUS_ROWS

    // ------------------------------------------------------------------------------------------------
    // KNOWN DEBT. Every setting that is exposed and read by NOTHING, one line each, by name.
    //
    // The contract (section 1.3) forbids these outright, so this list is a defect register and not a
    // permission: it exists because the census that FINDS them is worth more than the census that would
    // have to be weakened to stay green. Each of the three is a separate piece of work owned elsewhere;
    // repairing one means deleting its line here and pointing its row at the reader, and the equality
    // below makes both directions - a new dead setting, and a repaired one - a reviewable edit.
    // ------------------------------------------------------------------------------------------------

    struct DeadSetting
    {
        const char* Type;
        const char* Field;
    };

    constexpr DeadSetting kKnownDeadSettings[] = {
         // Reflected under Category( "Physics" ) beside Gravity, which IS read. Nothing consults it.
         { "SceneSettings", "PauseSimulation" },
         // "World Space" on the emitter. The word appears in the renderer's header only as prose about
         // the canvas's WorldSpace render mode, which is a different component's enum entirely - the
         // shared-name trap this suite's row shape exists to defeat.
         { "ParticleEmitterData", "WorldSpace" },
         // The UI element's pivot. UICanvasLayout resolves a rect from anchors and offsets and never
         // consults a pivot; nothing else in the UI reads it either.
         { "UILayoutData", "Pivot" },
    };

    // The repository root, found by walking up from wherever the test binary was started - the same
    // approach the font-baker test uses, so neither has to be run from one exact directory.
    std::string RepoRoot()
    {
        // Starts at "./" rather than "": an empty string is this function's "not found", and the root is
        // very often the directory the test was started in.
        std::string prefix = "./";
        for ( int up = 0; up < 6; ++up )
        {
            std::ifstream probe( prefix + "Desert/Desert/Source/Engine/ECS/Components.hpp" );
            if ( probe )
                return prefix;
            prefix += "../";
        }
        return {};
    }

    std::string ReadFile( const std::string& path )
    {
        std::ifstream in( path );
        if ( !in )
            return {};
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // `Foo.cpp` -> `Foo.hpp` and back. Returns the input unchanged for anything else, and a path that
    // does not exist simply reads as empty.
    std::string SiblingOf( const std::string& path )
    {
        if ( path.size() > 4 && path.compare( path.size() - 4, 4, ".cpp" ) == 0 )
            return path.substr( 0, path.size() - 4 ) + ".hpp";
        if ( path.size() > 4 && path.compare( path.size() - 4, 4, ".hpp" ) == 0 )
            return path.substr( 0, path.size() - 4 ) + ".cpp";
        return path;
    }

    const TypeInfo& Type( const char* name )
    {
        const TypeInfo* t = ReflectionRegistry::Get().Find( name );
        EXPECT_NE( t, nullptr ) << name << " is not registered - the codegen did not run";
        return *t;
    }

    std::vector<std::string> AnchorsOf( const Census& c )
    {
        std::vector<std::string> anchors{ c.Type };
        if ( c.Component )
            anchors.emplace_back( c.Component );
        if ( c.Accessor )
            anchors.emplace_back( c.Accessor );
        return anchors;
    }

    // Every reflected field appears in the table exactly once, and every table row names a real field.
    void CheckTableCoversTypeExactly( const TypeInfo& type, const Row* rows, std::size_t count )
    {
        for ( const FieldInfo& f : type.Fields )
        {
            const std::ptrdiff_t hits =
                 std::count_if( rows, rows + count, [&f]( const Row& r ) { return f.Name == r.Field; } );
            EXPECT_EQ( hits, 1 ) << type.Name << "::" << f.Name
                                 << " has no consumer row (or more than one). Every exposed setting must "
                                    "name either the code that reads it, the task that will, or the "
                                    "reason nothing does.";
        }

        for ( const Row* r = rows; r != rows + count; ++r )
        {
            const bool known = std::any_of( type.Fields.begin(), type.Fields.end(),
                                            [r]( const FieldInfo& f ) { return f.Name == r->Field; } );
            EXPECT_TRUE( known ) << r->Field << " is listed as consumed but is not a field of " << type.Name
                                 << " - a stale row outliving the field it described";

            const int kinds = ( r->Where != nullptr ) + ( r->Task != nullptr ) + ( r->Dead != nullptr );
            EXPECT_EQ( kinds, 1 ) << type.Name << "::" << r->Field
                                  << " must name exactly one of: a consumer file, an owing task, or the "
                                     "reason it is dead";
        }

        EXPECT_EQ( count, type.Fields.size() );
    }
} // namespace

TEST( SettingConsumers, EveryReflectedTypeIsUnderThisCensus )
{
    const auto& all = ReflectionRegistry::Get().All();

    for ( const auto& [name, info] : all )
    {
        const bool covered = std::any_of( std::begin( kCensus ), std::end( kCensus ),
                                          [&name]( const Census& c ) { return name == c.Type; } );
        EXPECT_TRUE( covered ) << name
                               << " is a reflected type with no consumer table. Every field it exposes "
                                  "reaches a Details panel; add a table naming who reads each one.";
    }

    for ( const Census& c : kCensus )
        EXPECT_NE( ReflectionRegistry::Get().Find( c.Type ), nullptr )
             << c.Type << " has a consumer table but is no longer reflected - a stale census entry";

    // The count is pinned as well as the membership, because the two fail differently: a type that loses
    // its REFLECT() drops out of `all` silently, and only the number says so.
    EXPECT_EQ( all.size(), std::size( kCensus ) );
    EXPECT_EQ( all.size(), 37u );
}

TEST( SettingConsumers, EveryFieldNamesItsConsumer )
{
    for ( const Census& c : kCensus )
    {
        SCOPED_TRACE( c.Type );
        CheckTableCoversTypeExactly( Type( c.Type ), c.Rows, c.Count );
    }
}

// THE ASSERTION Д23 EXISTS FOR. A WIRED row used to pass when the named file merely contained the
// field's name; У3 proved that vacuous by deleting the canvas's background draw and watching this test
// stay green, because three other components in the same file have a `Sprite` too.
//
// Now the file must contain an ANCHORED READ: a member access of the field on a receiver bound, in that
// same file, to this type (setting_consumers_reader.hpp explains the shapes accepted). Deleting the read
// removes the last anchored access and this goes red; renaming the local variable moves the binding and
// the read together and it does not.
TEST( SettingConsumers, EveryNamedConsumerActuallyReadsTheFieldItClaims )
{
    using namespace Desert::Tests::ConsumerText;

    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "repository root not found - run from the workspace root or build/Bin";

    for ( const Census& c : kCensus )
    {
        const std::vector<std::string> anchors = AnchorsOf( c );

        for ( const Row* r = c.Rows; r != c.Rows + c.Count; ++r )
        {
            if ( !r->Where )
                continue;

            const std::string text = StripCommentsAndLiterals( ReadFile( root + r->Where ) );
            ASSERT_FALSE( text.empty() ) << "named consumer " << r->Where << " could not be read";

            // A renderer keeps the component in a member declared in its HEADER and reads it in the
            // source, so the two files are one scope for the purpose of finding receivers. Without this
            // every `m_Data.X` in VolumetricCloudRenderer.cpp reads as unanchored, and twenty-two live
            // cloud settings look dead.
            const std::vector<std::string> receivers =
                 DeriveReceivers( text + "\n" + StripCommentsAndLiterals( ReadFile( root + SiblingOf( r->Where ) ) ),
                                  anchors );

            bool read = false;
            for ( const std::string& recv : receivers )
                read = read || ReceiverReadsField( text, recv, r->Field );
            for ( const std::string& anchor : anchors )
                read = read || AnchorReadsField( text, anchor, r->Field );

            EXPECT_TRUE( read ) << r->Where << " is named as the consumer of " << c.Type << "::" << r->Field
                                << " but contains no read of that field on a value of that type. Either "
                                   "the read was removed and the setting is now dead, or it moved to "
                                   "another file and this row must follow it.";
        }
    }
}

// The debt register, pinned in both directions.
//
// It is stated as an exact set rather than a count so that repairing one dead setting and introducing
// another cannot cancel out - which a count would have allowed, and which is precisely how a register
// like this stops being read.
TEST( SettingConsumers, TheKnownDeadSettingsAreExactlyThese )
{
    std::vector<std::string> fromTables;
    for ( const Census& c : kCensus )
        for ( const Row* r = c.Rows; r != c.Rows + c.Count; ++r )
            if ( r->Dead )
                fromTables.push_back( std::string( c.Type ) + "::" + r->Field );

    std::vector<std::string> registered;
    for ( const DeadSetting& d : kKnownDeadSettings )
        registered.push_back( std::string( d.Type ) + "::" + d.Field );

    std::sort( fromTables.begin(), fromTables.end() );
    std::sort( registered.begin(), registered.end() );

    EXPECT_EQ( fromTables, registered )
         << "a row marked DEAD is not in the known-debt register, or the register names a setting that is "
            "no longer dead. Both are edits somebody has to see.";

    // Three, and each of them is a defect that belongs to a task of its own. This number going UP is a
    // new dead setting; going DOWN is a repair. Neither may happen silently.
    EXPECT_EQ( registered.size(), 3u );
}

// A field name shared by four components makes a name-only row vacuous, and this is the case that proved
// it. The row shape above is the general fix; this stays as the specific one, because the canvas's
// backdrop is the exact setting that was dead, and an assertion on the expression that revives it is the
// cheapest possible regression test for that particular history.
//
// Spelled out in full rather than matched loosely: a regex that still matched after somebody rewrote the
// read would pass while testing nothing.
TEST( SettingConsumers, TheCanvasBackgroundIsReadAtTheCanvasAndNotJustNamedSomewhereInTheFile )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "repository root not found from the test's working directory";

    const std::string source = ReadFile( root + kCanvasRenderer );
    ASSERT_FALSE( source.empty() ) << kCanvasRenderer << " is missing or empty";

    EXPECT_NE( source.find( "HandleSet( canvasData.Sprite )" ), std::string::npos )
         << "UICanvasRenderer2D does not test the CANVAS's own background handle. The three other Sprite "
            "fields in this file (button, panel, image) keep the name alive whether or not the canvas's is "
            "read, which is how this setting stayed dead through a census that lists it as wired.";

    // And it is drawn, not merely inspected: the resolved image reaches the draw list.
    EXPECT_NE( source.find( "ResolveSpriteImage( canvasData.Sprite )" ), std::string::npos )
         << "the canvas background handle is tested but never resolved to an image";
}

// Slot A shipped WHOLE - format, loader, service, component, collector, packer, seam and cutout in one
// task - so it owes nothing either. The pin is what keeps that true.
TEST( SettingConsumers, TheHeroCloudComponentOwesNothing )
{
    const std::ptrdiff_t pending = std::count_if( std::begin( kHeroCloudRows ), std::end( kHeroCloudRows ),
                                                  []( const Row& r ) { return r.Task != nullptr; } );

    EXPECT_EQ( pending, 0 );
}

// The fog shipped WHOLE - component, pass and couplings in one task (Sky plan Phase 5) - so it owes
// nothing. This pin is what keeps that true: a field added without its reader turns this zero into a
// reviewable edit.
TEST( SettingConsumers, TheFogComponentOwesNothing )
{
    const std::ptrdiff_t pending = std::count_if( std::begin( kFogRows ), std::end( kFogRows ),
                                                  []( const Row& r ) { return r.Task != nullptr; } );

    EXPECT_EQ( pending, 0 );
}

// The sky component now owes NOTHING. The artistic gradient was always finished; the physical
// atmosphere was built in phases (Docs/Sky/UE_SKYATMOSPHERE_RESEARCH.md section 4) and Phase 3 — the
// camera aerial-perspective volume and its apply on opaque — consumed the last two fields that were
// carried without a reader, plus the Aerial Perspective Distance it added.
//
// The count stays as a count rather than becoming "no PENDING rows exist", because a later phase may
// well add a field before it adds its reader. When that happens the number rises in a reviewable edit
// instead of a field quietly joining the component with nobody accountable for it.
TEST( SettingConsumers, TheSkyComponentOwesNothing )
{
    const std::ptrdiff_t pending = std::count_if( std::begin( kSkyRows ), std::end( kSkyRows ),
                                                  []( const Row& r ) { return r.Task != nullptr; } );

    EXPECT_EQ( pending, 0 );
}

// The cloud component shipped the same way the fog did - component, packer, bake and march in one
// programme - so it owes nothing either. The count is a count rather than "no PENDING rows exist" for the
// reason the two above give: a later phase may well add a field before it adds its reader, and when that
// happens the number has to rise in a reviewable edit instead of a field quietly joining the component
// with nobody accountable for it.
//
// It matters more here than anywhere else in this file. A cloud parameter that does nothing still LOOKS
// like it does, because the sky it is supposed to change is already busy - which is precisely why this
// programme's contract forbids a knob that moves nothing.
TEST( SettingConsumers, TheCloudComponentOwesNothing )
{
    const std::ptrdiff_t pending = std::count_if( std::begin( kCloudRows ), std::end( kCloudRows ),
                                                  []( const Row& r ) { return r.Task != nullptr; } );

    EXPECT_EQ( pending, 0 );
}

// A slider can name its consumer and STILL not reach it, and this is the case that proves it.
//
// Light March Samples travels to the GPU through three separate ceilings: the Range on the PROPERTY, the
// std::clamp in Graphic::PackCloudParams, and a clamp written again inside the compute shader. All three
// were the literal 16. Raise the first two to 64 and forget the third and the artist drags the slider to
// 64, the payload carries 64, and the march silently uses 16 — a setting that reaches its consumer and is
// thrown away there, which no reflection test and no build can see.
//
// The first two ceilings are now one constant. The shader cannot include a C++ header, so its copy is
// checked the only way it can be: by reading the shader's own text. That is what makes this an assertion
// about the RELATION (contract 2.3.1) rather than three assertions about the number 64.
TEST( SettingConsumers, TheShaderClampsTheShadowRayAtTheSameCeilingTheSliderOffers )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "repository root not found from the test's working directory";

    const std::string path   = root + "Editor/Resources/Shaders/Programs/Clouds/CloudRaymarch.shader";
    const std::string source = ReadFile( path );
    ASSERT_FALSE( source.empty() ) << path << " is missing or empty";

    // The exact line the march clamps on. Written out in full rather than matched loosely, because a
    // regex that still matched after somebody rewrote the clamp would pass while testing nothing.
    const std::string expected = "int   lightSamples = int(clamp(u_CloudSunColour.w, 1.0f, " +
                                 std::to_string( Desert::ECS::kCloudLightMarchMaxSamples ) + ".0f));";

    EXPECT_NE( source.find( expected ), std::string::npos )
         << "CloudRaymarch.shader does not clamp the shadow ray's sample count at "
         << Desert::ECS::kCloudLightMarchMaxSamples << ", which is the ceiling the slider offers and the "
         << "payload packs. Expected to find:\n  " << expected;
}

// The same three-ceilings shape as the test above, for the multiple-scattering octave count.
//
// It was three literal 3s — the PROPERTY's Range, the std::clamp in Graphic::PackCloudParams and a clamp
// written again in the march — and they agreed only because nobody had moved one. Р18 had reason to move
// it (the question was whether more octaves recover what the approximation loses at a physical
// extinction; measured, they make it worse, which is recorded on the constant itself), and the first
// thing that question needs is for the three to be one.
//
// THE CLAMP IS IN THE HEADER, NOT THE SHADER, and the difference is the point of the change rather than
// an accident of it: the series moved into Common/CloudLighting.glslh so a test could drive it as C++.
// This assertion therefore reads a .glslh where its neighbour above reads a .shader.
TEST( SettingConsumers, TheScatteringSeriesClampsItsOctavesAtTheSameCeilingTheSliderOffers )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "repository root not found from the test's working directory";

    const std::string path   = root + "Editor/Resources/Shaders/Common/CloudLighting.glslh";
    const std::string source = ReadFile( path );
    ASSERT_FALSE( source.empty() ) << path << " is missing or empty";

    const std::string expected =
         "#define CLOUD_MAX_SCATTER_OCTAVES " + std::to_string( Desert::ECS::kCloudMultiScatterMaxOctaves );

    EXPECT_NE( source.find( expected ), std::string::npos )
         << "CloudLighting.glslh does not cap the multiple-scattering series at "
         << Desert::ECS::kCloudMultiScatterMaxOctaves << ", which is the ceiling the slider offers and the "
         << "payload packs. Expected to find:\n  " << expected;
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}

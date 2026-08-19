#pragma once

#include <glm/glm.hpp>

#include <Engine/Assets/Common.hpp>
#include <Engine/Reflection/ReflectionMacros.hpp>

namespace Desert::Core
{
    // Anti-aliasing technique. Deliberately no MSAA (incompatible with the planned deferred pipeline);
    // these are all post-process. TAA/DLSS are intentionally absent until motion-vector + jitter
    // infrastructure exists (built alongside deferred).
    enum class AntiAliasingMode : int
    {
        None = 0,
        FXAA,
        SMAA,
    };

    // Directional-shadow debug visualization (CSM). Off = normal lit; ShadowFactor = raw grayscale shadow
    // term; Cascades = tint each fragment by the cascade that shadows it (red/green/blue/yellow front→back).
    enum class ShadowDebugMode : int
    {
        Off = 0,
        ShadowFactor,
        Cascades,
    };

    // Global texture sampler filter (applies to all sampled images; live — recreates samplers on change).
    // Must match Graphic::TextureFilterMode.
    enum class TextureFilter : int
    {
        Nearest     = 0,
        Bilinear    = 1,
        Trilinear   = 2,
        Anisotropic = 3,
    };

    // Rendering path. Forward = the classic one-pass lit shading (default, always works). Deferred = a
    // G-buffer pass + a screen-space lighting pass, which scales to many dynamic lights (city lamps/windows)
    // and unlocks screen-space GI/AO. Built incrementally behind this toggle so Forward stays the safe path.
    enum class RenderPath : int
    {
        Forward  = 0,
        Deferred = 1,
    };

    // Deferred G-buffer debug visualization (UE-style buffer view). Off = normal lit; the others fill the
    // screen with a raw G-buffer channel so the deferred passes can be inspected. Only used in the Deferred path.
    enum class DeferredDebugMode : int
    {
        Off       = 0,
        Albedo    = 1,
        Normal    = 2,
        Metallic  = 3,
        Roughness = 4,
        AO        = 5,
        GI        = 6, // indirect light only (whichever GIMode is active) — for judging GI in isolation
        LightComplexity    = 7, // per-pixel count of point/spot light volumes, heat-mapped (UE-style)
        Overdraw           = 8, // additive re-raster of all meshes -> heat-mapped overdraw count (both paths)
        MaterialComplexity = 9, // per-pixel sampled-texture count (from GBufferC.w), heat-mapped (UE-style)
    };

    // Source of the one-bounce indirect light (Deferred path only).
    //  ScreenSpace — gathers from sun-lit G-buffer neighbours right inside the lighting pass. Cheap and
    //                self-contained, but ONLY geometry currently on screen can bounce, and there is no
    //                denoiser, so a wide radius reads as noise.
    //  RSM         — bounces light off everything the SUN sees (off-screen geometry included) via a
    //                reflective shadow map, resolved into its own buffer and temporally accumulated.
    //                Better and stabler; costs one sun-view raster pass plus two fullscreen passes.
    enum class GIMode : int
    {
        Off         = 0,
        ScreenSpace = 1,
        RSM         = 2,
    };

    // The curve that maps HDR scene luminance onto the 0..1 the display can show. Not a compatibility
    // switch: both branches are authored features, and which one a scene is graded through is a property
    // of the scene, the way film stock was a property of the shoot.
    //
    //  ACES     — the filmic curve Unreal ships as its default. It is OURS by default too (decision D-10,
    //             Docs/Clouds/ANALYSIS_APPROACH.md §7) for one measurable reason: the reference frame this
    //             programme calibrates the sky against was captured through it, so until both sides use
    //             the same operator, every number in Docs/Clouds/CALIBRATION.md measures the difference
    //             between two TONEMAPPERS and not between two skies. It takes no white point — the ODT
    //             the fit reproduces carries its own.
    //  Reinhard — extended Reinhard with an explicit WhitePoint (below). Every scene authored before
    //             2026-08-19 had its Exposure and WhitePoint chosen by eye on this curve, which is why it
    //             stays selectable and why the scene migration pins it onto files that predate the field.
    enum class TonemapOperator : int
    {
        ACES     = 0,
        Reinhard = 1,
    };

    // Reflected (REFLECT/PROPERTY) so the whole block (de)serializes generically via the reflection
    // serializer (no hand-written mirror) and the editor can build its panel from the same metadata.
    struct SceneSettings
    {
        REFLECT()

        // Selection outline (Jump Flood) moved OUT of scene settings into EditorPreferences: it is an
        // editor-only viewport visualization (runtime builds have no selection), not a scene property.
        // See Editor::EditorPreferences (OutlineColor/Width/Smoothness/EnableOutline), pushed to the
        // renderer each frame via SceneRenderer::SetOutlineSettings.

        // Rendering path (Forward default; Deferred is built incrementally behind this toggle).
        PROPERTY( DisplayName( "Render Path" ), Category( "Rendering" ) )
        RenderPath RenderingPath = RenderPath::Deferred;

        PROPERTY( DisplayName( "Deferred Debug" ), Category( "Rendering" ) )
        DeferredDebugMode DeferredDebug = DeferredDebugMode::Off;

        // Deferred screen-space effects (Deferred path only). Both cost a full-screen multi-sample pass —
        // turn off for maximum FPS.
        PROPERTY( DisplayName( "Enable SSAO" ), Category( "Rendering" ) )
        bool EnableSSAO = true;

        PROPERTY( DisplayName( "Global Illumination" ), Category( "Rendering" ) )
        GIMode GlobalIllumination = GIMode::ScreenSpace;
        // ONE knob for both GI modes, but they are not on the same scale: the screen-space gather is
        // bright at ~2, while the RSM gather is deliberately dim (32 taps, 1/d^2 with a distance floor)
        // and wants ~6. Switching to RSM will look flat until this is raised — that is expected, not a bug.
        PROPERTY( DisplayName( "GI Intensity" ), Category( "Rendering" ), Range( 0.0f, 20.0f ) )
        float GIIntensity = 2.0f;

        // Screen-space reflections: mirrors/metal/polished floors reflect what is on screen. Traced at
        // quarter cost then temporally denoised; still bound by the usual SSR limit (off-screen and
        // occluded geometry cannot reflect).
        PROPERTY( DisplayName( "Enable SSR" ), Category( "Rendering" ) )
        bool EnableSSR = false;
        PROPERTY( DisplayName( "SSR Intensity" ), Category( "Rendering" ), Range( 0.0f, 1.0f ) )
        float SSRIntensity = 1.0f;
        PROPERTY( DisplayName( "SSR Max Distance" ), Category( "Rendering" ), Range( 1.0f, 200.0f ) )
        float SSRMaxDistance = 40.0f;

        // Environment: skybox brightness now lives on the SkyboxComponent (entity) as
        // SkyboxComponent::Intensity — applied in the Skybox pass. Procedural sky lives there too. The old
        // EnvironmentMapIntensity / SkyboxLOD scene-global knobs were dead (no render consumer) and removed;
        // stale copies in old scene files are simply ignored on load.
        PROPERTY( DisplayName( "Enable Shadows" ), Category( "Shadows" ) )
        bool  EnableShadows           = true;
        PROPERTY( DisplayName( "Shadow Bias" ), Category( "Shadows" ), Range( 0.0f, 0.05f ) )
        float ShadowBias              = 0.005f;
        PROPERTY( DisplayName( "Cascade Split Lambda" ), Category( "Shadows" ), Range( 0.0f, 1.0f ) )
        float CascadeSplitLambda      = 0.6f;
        PROPERTY( DisplayName( "Shadow Debug" ), Category( "Shadows" ) )
        ShadowDebugMode ShadowDebug   = ShadowDebugMode::Off;

        // Post-processing. The operator comes FIRST because it decides what the knobs under it mean:
        // WhitePoint belongs to Reinhard alone and the editor hides it in the other mode.
        PROPERTY( DisplayName( "Tonemapper" ), Category( "Post Processing" ) )
        TonemapOperator Tonemapper = TonemapOperator::ACES;
        PROPERTY( DisplayName( "Exposure" ), Category( "Post Processing" ), Range( 0.0f, 10.0f ) )
        float Exposure       = 1.0f; // manual exposure (used when auto-exposure is off)
        PROPERTY( DisplayName( "Gamma" ), Category( "Post Processing" ), Range( 1.0f, 3.0f ) )
        float Gamma          = 2.2f;
        // REINHARD ONLY — the luminance that tonemaps to pure white. 1.0 makes extended Reinhard the
        // IDENTITY, which is what this pass used to hard-code, and why every HDR highlight clipped to a
        // flat white silhouette instead of keeping its shading.
        //
        // The ACES branch does not read it and must not be given a version of it: the fit reproduces a
        // specific ODT, whose white is part of the curve. Inventing a knob that slides that curve would
        // make our "ACES" a different operator from the one the reference frame was shot through, which
        // is precisely the confound D-10 exists to remove. So the editor shows this slider only in
        // Reinhard mode — a slider that moves nothing in the current mode is the dead setting
        // DEV_CONTRACT §1.3 forbids.
        PROPERTY( DisplayName( "White Point (Reinhard)" ), Category( "Post Processing" ), Range( 1.0f, 20.0f ) )
        float WhitePoint = 8.0f;

        // Auto-exposure / eye adaptation.
        PROPERTY( DisplayName( "Auto Exposure" ), Category( "Post Processing" ) )
        bool  AutoExposure       = false;
        PROPERTY( DisplayName( "Auto Exposure Key" ), Category( "Post Processing" ), Range( 0.0f, 1.0f ) )
        float AutoExposureKey    = 0.18f; // middle-grey target
        PROPERTY( DisplayName( "Auto Exposure Speed" ), Category( "Post Processing" ), Range( 0.0f, 10.0f ) )
        float AutoExposureSpeed  = 1.5f;  // higher = adapts faster
        PROPERTY( DisplayName( "Auto Exposure Min" ), Category( "Post Processing" ), Range( 0.0f, 5.0f ) )
        float AutoExposureMin    = 0.02f; // luminance clamp
        PROPERTY( DisplayName( "Auto Exposure Max" ), Category( "Post Processing" ), Range( 0.0f, 20.0f ) )
        float AutoExposureMax    = 8.0f;
        PROPERTY( DisplayName( "Anti-Aliasing" ), Category( "Post Processing" ) )
        AntiAliasingMode AA  = AntiAliasingMode::FXAA;
        PROPERTY( DisplayName( "Enable Bloom" ), Category( "Post Processing" ) )
        bool  EnableBloom    = false; // off by default -> no glow out of the box
        PROPERTY( DisplayName( "Bloom Threshold" ), Category( "Post Processing" ), Range( 0.0f, 10.0f ) )
        float BloomThreshold = 2.0f;
        PROPERTY( DisplayName( "Bloom Intensity" ), Category( "Post Processing" ), Range( 0.0f, 5.0f ) )
        float BloomIntensity = 0.8f;
        PROPERTY( DisplayName( "Lens Dispersion" ), Category( "Post Processing" ), Range( 0.0f, 3.0f ) )
        float LensDispersion = 0.0f; // chromatic rainbow fringe on the bloom halo (glare); 0 = off

        // Lens flare — the camera's response to a very bright source in frame (the sun disc above all).
        // Its own category rather than more entries in Post Processing, which is already the longest
        // section in the panel; UE groups these the same way, under Bloom on the post-process volume.
        //
        // These are the LENS's properties, not the sun's: the same glass flares whatever is bright, which
        // is why they sit here beside Bloom and not on the directional light (where the light SHAFTS
        // live, because a shaft is scattering in the world rather than in the optics).
        //
        // Every one of them is content. The pass hardcodes no colour, no ghost count and no spacing —
        // authoring a different flare is a scene edit. Off by default, so no existing scene changes.
        PROPERTY( DisplayName( "Enable Lens Flare" ), Category( "Lens Flare" ) )
        bool EnableLensFlare = false;
        PROPERTY( DisplayName( "Intensity" ), Category( "Lens Flare" ), Range( 0.0f, 5.0f ) )
        float LensFlareIntensity = 0.35f;
        PROPERTY( DisplayName( "Tint" ), Category( "Lens Flare" ), Color )
        glm::vec3 LensFlareTint = glm::vec3( 1.0f );
        // HDR luminance a pixel must exceed to flare at all. Authored high enough that in a physical sky
        // only the sun disc qualifies — this, not a radial mask, is what makes it a SUN flare.
        PROPERTY( DisplayName( "Threshold" ), Category( "Lens Flare" ), Range( 0.0f, 50.0f ) )
        float LensFlareThreshold = 4.0f;

        // Ghosts: internal reflections, spaced along the sun->screen-centre axis. Spacing is the fraction
        // of that axis between one ghost and the next, so counts past 1/Spacing land beyond the centre —
        // which is where the second half of a real ghost train sits.
        PROPERTY( DisplayName( "Ghost Count" ), Category( "Lens Flare" ), Range( 0.0f, 8.0f ) )
        int LensFlareGhostCount = 4;
        PROPERTY( DisplayName( "Ghost Spacing" ), Category( "Lens Flare" ), Range( 0.05f, 1.5f ) )
        float LensFlareGhostSpacing = 0.35f;
        // Magnification, not a screen size: a ghost is an IMAGE of the source, so 1.0 draws it the size
        // the sun already is (a few pixels) and the useful range is well above that. Near/far are the two
        // ends of the train's ramp.
        PROPERTY( DisplayName( "Ghost Size Near" ), Category( "Lens Flare" ), Range( 0.05f, 16.0f ) )
        float LensFlareGhostSizeNear = 1.0f;
        PROPERTY( DisplayName( "Ghost Size Far" ), Category( "Lens Flare" ), Range( 0.05f, 16.0f ) )
        float LensFlareGhostSizeFar = 3.0f;
        // The ghost train's authored tint ramp: ghost i takes mix(Inner, Outer, i/(count-1)), so any
        // count gets a full ramp and no palette lives in the shader.
        PROPERTY( DisplayName( "Ghost Tint Inner" ), Category( "Lens Flare" ), Color )
        glm::vec3 LensFlareGhostTintInner = glm::vec3( 1.0f, 0.86f, 0.62f );
        PROPERTY( DisplayName( "Ghost Tint Outer" ), Category( "Lens Flare" ), Color )
        glm::vec3 LensFlareGhostTintOuter = glm::vec3( 0.45f, 0.68f, 1.0f );

        // Halo: the ring the front element scatters, centred on the sun. Radius is in aspect-corrected
        // screen UV, so it stays a circle at any window shape.
        PROPERTY( DisplayName( "Halo Intensity" ), Category( "Lens Flare" ), Range( 0.0f, 3.0f ) )
        float LensFlareHaloIntensity = 0.25f;
        PROPERTY( DisplayName( "Halo Radius" ), Category( "Lens Flare" ), Range( 0.02f, 1.0f ) )
        float LensFlareHaloRadius = 0.18f;

        // Anamorphic streak: a cylindrical element smearing the source along one axis. Angle in degrees,
        // 0 = horizontal (the blue horizontal streak anamorphic lenses are known for).
        //
        // Its intensity range is an order above the others on purpose: the streak is an AVERAGE over its
        // taps, so a source covering a few of 64 taps arrives ~20x weaker than a ghost, which is a direct
        // sample. Both are honest; they just meet the authored number at different scales.
        PROPERTY( DisplayName( "Streak Intensity" ), Category( "Lens Flare" ), Range( 0.0f, 30.0f ) )
        float LensFlareStreakIntensity = 8.0f;
        PROPERTY( DisplayName( "Streak Length" ), Category( "Lens Flare" ), Range( 0.0f, 1.0f ) )
        float LensFlareStreakLength = 0.35f;
        PROPERTY( DisplayName( "Streak Angle" ), Category( "Lens Flare" ), Range( -180.0f, 180.0f ) )
        float LensFlareStreakAngle = 0.0f;

        // Chromatic shift: how far apart the three channels are read within each feature. The fringe is
        // the SCENE dispersed, not a colour painted on, so 0 gives a perfectly neutral flare.
        PROPERTY( DisplayName( "Chromatic Shift" ), Category( "Lens Flare" ), Range( 0.0f, 1.0f ) )
        float LensFlareChromaShift = 0.15f;

        // Textures
        PROPERTY( DisplayName( "Texture Filter" ), Category( "Textures" ) )
        TextureFilter TextureFilterMode = TextureFilter::Trilinear;
        PROPERTY( DisplayName( "Anisotropy" ), Category( "Textures" ), Range( 1.0f, 16.0f ) )
        int           Anisotropy        = 8; // 1/2/4/8/16x — used only in Anisotropic filter mode

        // Debug visualization
        PROPERTY( DisplayName( "Show Grid" ), Category( "Debug" ) )
        bool      ShowGrid             = true; // infinite editor scene grid
        PROPERTY( DisplayName( "Show Bounding Boxes" ), Category( "Debug" ) )
        bool      ShowBoundingBoxes    = false;
        PROPERTY( DisplayName( "BB Color" ), Category( "Debug" ), Color )
        glm::vec3 BoundingBoxColor     = glm::vec3( 0.25f, 0.95f, 0.35f );
        PROPERTY( DisplayName( "BB Line Width" ), Category( "Debug" ), Range( 1.0f, 10.0f ) )
        float     BoundingBoxLineWidth = 1.5f;
        PROPERTY( DisplayName( "Show Colliders" ), Category( "Debug" ) )
        bool      ShowColliders        = true; // green physics-collider wireframes (editor aid, UE-style)
        // ShowNormals / LightingDebug are driven by the viewport View Mode dropdown (Normals /
        // Light Complexity), not a Scene Settings toggle — kept as plain fields (no PROPERTY, so they
        // neither serialize nor show in a reflected panel), still consumed by the mesh renderer.
        bool      ShowNormals          = false;
        PROPERTY( DisplayName( "Wireframe" ), Category( "Debug" ) )
        bool      WireframeMode        = false;
        PROPERTY( DisplayName( "Mesh LOD (auto)" ), Category( "Debug" ),
                  Tooltip( "Distance-based mesh level of detail. LOD0 (near) is identical geometry." ) )
        bool      MeshLOD              = true;
        bool      LightingDebug        = false; // see note above — no longer a Scene Settings property

        // Other scene-wide settings
        PROPERTY( DisplayName( "Gravity" ), Category( "Physics" ), Range( 0.0f, 5000.0f ) )
        float Gravity = 981.0f; // For physics simulation (cm/s^2 — 1 unit = 1 cm)
        PROPERTY( DisplayName( "Pause Simulation" ), Category( "Physics" ) )
        bool  PauseSimulation = false;

        // Wind — a SHARED environment force, deliberately scene-global (like Gravity), NOT owned by the
        // Skybox. It is the single source of truth for the wind that drives grass/foliage sway today and
        // hair and cloth next. Consumers read it via SceneRenderer::GetWind() (renderers) so
        // one direction/strength moves everything coherently.
        PROPERTY( DisplayName( "Wind Direction" ), Category( "Wind" ), Range( 0.0f, 360.0f ) )
        float WindDirection  = 20.0f; // compass heading on the ground (XZ) plane, degrees
        PROPERTY( DisplayName( "Wind Strength" ), Category( "Wind" ), Range( 0.0f, 1.0f ) )
        float WindStrength   = 0.15f; // base force / foliage sway amplitude
        PROPERTY( DisplayName( "Wind Turbulence" ), Category( "Wind" ), Range( 0.0f, 3.0f ) )
        float WindTurbulence = 1.0f;  // gustiness (reserved for foliage/hair/cloth response)

        // Water moved OUT of global scene settings: it is a gameplay value, not a render setting. It now
        // lives on the spawned "Water" entity (World.spawnWater drops a plane at the level); World.waterLevel
        // reads that entity's height, so the swim script keeps working without a global knob here.

        // Time of Day was removed: the sun's single source of truth is the directional-light ENTITY
        // (its position encodes the direction; sky + lighting follow it). Old scene files may still
        // carry the fields — unknown keys are ignored on load.

        // Splash screen: an image the standalone Runtime shows full-screen when THIS scene loads (the boot
        // scene's splash is the game's startup splash). Duration 0 = no splash; Fade = in/out seconds.
        PROPERTY( DisplayName( "Splash Sprite" ), Category( "Splash" ) )
        Assets::AssetHandle SplashSprite;
        PROPERTY( DisplayName( "Splash Duration" ), Category( "Splash" ), Range( 0.0f, 10.0f ) )
        float SplashDuration = 0.0f;
        PROPERTY( DisplayName( "Splash Fade" ), Category( "Splash" ), Range( 0.0f, 3.0f ) )
        float SplashFade = 0.4f;
    };
} // namespace Desert::Core
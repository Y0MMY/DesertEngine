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
        // 6 is reserved (an internal GI-only debug in the deferred shader).
        LightComplexity    = 7, // per-pixel count of point/spot light volumes, heat-mapped (UE-style)
        Overdraw           = 8, // additive re-raster of all meshes -> heat-mapped overdraw count (both paths)
        MaterialComplexity = 9, // per-pixel sampled-texture count (from GBufferC.w), heat-mapped (UE-style)
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
        PROPERTY( DisplayName( "Enable SSGI" ), Category( "Rendering" ) )
        bool EnableSSGI = true;

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

        // Post-processing
        PROPERTY( DisplayName( "Exposure" ), Category( "Post Processing" ), Range( 0.0f, 10.0f ) )
        float Exposure       = 1.0f; // manual exposure (used when auto-exposure is off)
        PROPERTY( DisplayName( "Gamma" ), Category( "Post Processing" ), Range( 1.0f, 3.0f ) )
        float Gamma          = 2.2f;

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
        PROPERTY( DisplayName( "Gravity" ), Category( "Physics" ), Range( 0.0f, 50.0f ) )
        float Gravity         = 9.81f; // For physics simulation
        PROPERTY( DisplayName( "Pause Simulation" ), Category( "Physics" ) )
        bool  PauseSimulation = false;

        // Wind — a SHARED environment force, deliberately scene-global (like Gravity), NOT owned by the
        // Skybox. It is the single source of truth for the wind that drives grass/foliage sway today and
        // cloud drift, hair and cloth next. Consumers read it via SceneRenderer::GetWind() (renderers) so
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
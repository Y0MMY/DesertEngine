#pragma once

#include <glm/glm.hpp>

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
    };

    // Reflected (REFLECT/PROPERTY) so the whole block (de)serializes generically via the reflection
    // serializer (no hand-written mirror) and the editor can build its panel from the same metadata.
    struct SceneSettings
    {
        REFLECT()

        // Outline settings (Jump Flood). Width and smoothness are in screen pixels.
        PROPERTY( DisplayName( "Outline Color" ), Category( "Outline" ), Color )
        glm::vec3 OutlineColor{ 1.0f, 0.5f, 0.0f }; // Orange by default
        PROPERTY( DisplayName( "Outline Width" ), Category( "Outline" ), Range( 0.0f, 20.0f ) )
        float     OutlineWidth      = 4.0f;
        PROPERTY( DisplayName( "Outline Smoothness" ), Category( "Outline" ), Range( 0.0f, 10.0f ) )
        float     OutlineSmoothness = 2.0f;
        PROPERTY( DisplayName( "Enable Outline" ), Category( "Outline" ) )
        bool      EnableOutline     = true;

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

        // Environment settings
        PROPERTY( DisplayName( "Env Map Intensity" ), Category( "Environment" ), Range( 0.0f, 10.0f ) )
        float EnvironmentMapIntensity = 1.0f;
        PROPERTY( DisplayName( "Skybox LOD" ), Category( "Environment" ), Range( 0.0f, 10.0f ) )
        float SkyboxLOD               = 0.0f; // Level of detail for skybox
        // NOTE: procedural sky now lives on the SkyboxComponent (entity), not here.
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
        PROPERTY( DisplayName( "Show Normals" ), Category( "Debug" ) )
        bool      ShowNormals          = false;
        PROPERTY( DisplayName( "Wireframe" ), Category( "Debug" ) )
        bool      WireframeMode        = false;
        PROPERTY( DisplayName( "Light Debug" ), Category( "Debug" ) )
        bool      LightingDebug        = false;

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

        // Water: a scene-global water surface level. The character-controller script reads it (World.waterLevel)
        // and switches to swimming (buoyancy) when the body drops below it. A visible water plane can be
        // spawned via World.spawnWater(level, size).
        PROPERTY( DisplayName( "Water Enabled" ), Category( "Water" ) )
        bool  WaterEnabled = false;
        PROPERTY( DisplayName( "Water Level" ), Category( "Water" ), Range( -100.0f, 100.0f ) )
        float WaterLevel   = 0.0f;

        // Time of Day — an OPT-IN day/night cycle (default off, so existing scenes are untouched). When on,
        // the DayNightSystem drives the scene's directional light (the "sun"): its DIRECTION follows the hour
        // and its INTENSITY fades to 0 at night (peak = SunPeakIntensity). The procedural sky already reacts
        // to the sun's elevation, so day/sunset/night colours follow for free.
        PROPERTY( DisplayName( "Enable Day/Night" ), Category( "Time of Day" ) )
        bool  EnableDayNight   = false;
        PROPERTY( DisplayName( "Time of Day" ), Category( "Time of Day" ), Range( 0.0f, 24.0f ) )
        float TimeOfDay        = 12.0f; // hours [0,24); 12 = noon
        PROPERTY( DisplayName( "Day Length (s)" ), Category( "Time of Day" ), Range( 0.0f, 600.0f ) )
        float DayLengthSeconds = 0.0f;  // real seconds for a full 24h cycle; 0 = frozen (manual scrub)
        PROPERTY( DisplayName( "Sun Peak Intensity" ), Category( "Time of Day" ), Range( 0.0f, 10.0f ) )
        float SunPeakIntensity = 3.0f;  // noon directional intensity (scaled down toward night)
    };
} // namespace Desert::Core
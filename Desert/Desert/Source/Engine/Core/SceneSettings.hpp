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
    };
} // namespace Desert::Core
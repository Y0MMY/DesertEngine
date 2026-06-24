#pragma once

#include <glm/glm.hpp>

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

    struct SceneSettings
    {
        // Outline settings (Jump Flood). Width and smoothness are in screen pixels.
        glm::vec3 OutlineColor{ 1.0f, 0.5f, 0.0f }; // Orange by default
        float     OutlineWidth      = 4.0f;
        float     OutlineSmoothness = 2.0f;
        bool      EnableOutline     = true;

        // Environment settings
        float EnvironmentMapIntensity = 1.0f;
        float SkyboxLOD               = 0.0f; // Level of detail for skybox
        bool  EnableShadows           = true;
        float ShadowBias              = 0.005f;

        // Post-processing (example)
        float Exposure       = 1.0f; // manual exposure (used when auto-exposure is off)
        float Gamma          = 2.2f;

        // Auto-exposure / eye adaptation. When on, exposure is driven by the measured scene luminance
        // (toward AutoExposureKey), adapting over time at AutoExposureSpeed; clamped to [Min,Max] luma.
        bool  AutoExposure       = false;
        float AutoExposureKey    = 0.18f; // middle-grey target
        float AutoExposureSpeed  = 1.5f;  // higher = adapts faster
        float AutoExposureMin    = 0.02f; // luminance clamp
        float AutoExposureMax    = 8.0f;
        AntiAliasingMode AA  = AntiAliasingMode::FXAA; // post-process AA technique (None/FXAA/SMAA)
        bool  EnableBloom    = false; // off by default -> no glow out of the box
        // HDR luma a pixel must exceed (pre-tonemap) to bloom. Raised from 1.0 so ordinary fully-lit
        // surfaces don't bloom; objects glow by setting Emissive Intensity ABOVE this. (No auto-exposure
        // yet, so this is an absolute value tied to scene brightness — bright IBL scenes may need ~3.)
        float BloomThreshold = 2.0f;
        float BloomIntensity = 0.8f;

        // Debug visualization
        bool ShowBoundingBoxes = false;
        bool ShowNormals       = false;
        bool WireframeMode     = false;

        // Other scene-wide settings
        float Gravity         = 9.81f; // For physics simulation
        bool  PauseSimulation = false;

        void Serialize( const Common::Filepath& filepath ) const{}
        bool Deserialize(const Common::Filepath& filepath) { return false; }
    };
} // namespace Desert::Core
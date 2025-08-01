#pragma once

#include <glm/glm.hpp>

namespace Desert::Core
{
    struct SceneSettings
    {
        // Outline settings
        glm::vec3 OutlineColor{ 1.0f, 0.5f, 0.0f }; // Orange by default
        float     OutlineWidth  = 1.05f;
        bool      EnableOutline = true;

        // Environment settings
        float EnvironmentMapIntensity = 1.0f;
        float SkyboxLOD               = 0.0f; // Level of detail for skybox
        bool  EnableShadows           = true;
        float ShadowBias              = 0.005f;

        // Post-processing (example)
        float Exposure       = 1.0f;
        float Gamma          = 2.2f;
        bool  EnableFXAA     = true;
        bool  EnableBloom    = false;
        float BloomThreshold = 1.0f;
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
#pragma once

#include <ImGui/imgui.h>

#include <Engine/Desert.hpp>

namespace Desert::Editor
{
    class Widgets
    {
    public:
        static void DrawVec3Control( const std::string& label, glm::vec3& values, float resetValue = 0.0f );
        static void DrawDirectionWidget( const std::string& label, glm::vec3& direction );
        static void DrawLightColorControl( const std::string& label, glm::vec3& color );
        static void DrawLightIntensityControl( const std::string& label, float& intensity, float min = 0.0f,
                                          float max = 5.0f );
    };
} // namespace Desert::Editor
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
        static void DrawColorControl( const std::string& label, glm::vec3& color );
        static void DrawFloatControl( const std::string& label, float& value, float step = 0.1f, float min = 0.0f,
                                      float max = 0.0f, const char* format = "%.2f" );
        static void DrawToggleControl( const std::string& label, bool& value );
    };
} // namespace Desert::Editor
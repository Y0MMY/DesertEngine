#pragma once

#include <Engine/Desert.hpp>
#include <imgui/imgui.h>

namespace Desert::Editor
{
    class LightGizmoRenderer
    {
    public:
        explicit LightGizmoRenderer( const std::shared_ptr<Desert::Core::Scene>& scene );
        ~LightGizmoRenderer() = default;

        void Render( float width, float height, float xpos, float ypos );

    private:
        void RenderPointLights( const std::shared_ptr<Desert::Core::Camera>& camera, float width, float height,
                                float xpos, float ypos );
        void DrawAxisAlignedCircle( ImDrawList* drawList, const glm::vec3& center, float radius, int segments,
                                    const glm::vec3& axis1, const glm::vec3& axis2, const glm::mat4& mvp,
                                    float width, float height, float xpos, float ypos, ImU32 color );

        void DrawLightRadiusSphere( const std::shared_ptr<Desert::Core::Camera>& camera, const glm::vec3& worldPos,
                                    float radius, float width, float height, float windowX, float windowY,
                                    float iconCenterX, float iconCenterY );

    private:
        std::shared_ptr<Desert::Core::Scene> m_Scene;
    };
} // namespace Desert::Editor
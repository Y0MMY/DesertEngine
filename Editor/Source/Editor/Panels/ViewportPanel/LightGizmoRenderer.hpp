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
        void RenderSelectedLightGizmo( const std::shared_ptr<Desert::Core::Camera>& camera,
                                       const glm::vec3& position, float radius, float width, float height,
                                       ImDrawList* drawList );

    private:
        std::shared_ptr<Desert::Core::Scene> m_Scene;
    };
} // namespace Desert::Editor
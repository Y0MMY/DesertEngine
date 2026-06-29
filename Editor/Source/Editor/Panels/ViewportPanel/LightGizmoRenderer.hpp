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
        void RenderSpotLights( const std::shared_ptr<Desert::Core::Camera>& camera, float width, float height,
                               float xpos, float ypos );
        // Camera entities (CameraComponent): billboard icon + wireframe view frustum.
        void RenderCameras( const std::shared_ptr<Desert::Core::Camera>& camera, float width, float height,
                            float xpos, float ypos );
        // Physics colliders (ColliderComponent): green Box/Sphere/Capsule wireframes (UE-style).
        void RenderColliders( const std::shared_ptr<Desert::Core::Camera>& camera, float width, float height,
                              float xpos, float ypos );
        // Skeleton Edit mode: draw the selected skinned mesh's bind-pose skeleton (bone heads + parent->child
        // links) as an overlay, with the bone-tree-selected bone highlighted. Read-only (Phase 1).
        void RenderSkeleton( const std::shared_ptr<Desert::Core::Camera>& camera, float width, float height,
                             float xpos, float ypos );
        // Draws a world-space line segment, clipping the endpoint that crosses the editor camera's near
        // plane (so a segment dipping behind the camera never wraps across the whole viewport).
        void DrawWorldLine( ImDrawList* drawList, const glm::vec3& a, const glm::vec3& b, const glm::mat4& mvp,
                            float width, float height, float windowX, float windowY, ImU32 color,
                            float thickness = 1.5f );
        void DrawAxisAlignedCircle( ImDrawList* drawList, const glm::vec3& center, float radius, int segments,
                                    const glm::vec3& axis1, const glm::vec3& axis2, const glm::mat4& mvp,
                                    float width, float height, float xpos, float ypos, ImU32 color );

        void DrawLightRadiusSphere( const std::shared_ptr<Desert::Core::Camera>& camera, const glm::vec3& worldPos,
                                    float radius, float width, float height, float windowX, float windowY,
                                    float iconCenterX, float iconCenterY );

        void DrawSpotCone( const std::shared_ptr<Desert::Core::Camera>& camera, const glm::vec3& apex,
                           const glm::vec3& dir, float outerAngleDeg, float range, float width, float height,
                           float windowX, float windowY );

    private:
        std::shared_ptr<Desert::Core::Scene> m_Scene;
    };
} // namespace Desert::Editor
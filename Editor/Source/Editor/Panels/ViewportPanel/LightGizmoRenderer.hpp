#pragma once

#include <Engine/Desert.hpp>
#include <imgui/imgui.h>

#include <utility>
#include <vector>

namespace Desert::Editor
{
    class LightGizmoRenderer
    {
    public:
        explicit LightGizmoRenderer( const std::shared_ptr<Desert::Core::Scene>& scene );
        ~LightGizmoRenderer() = default;

        void Render( float width, float height, float xpos, float ypos );

        // Nearest skeleton bone head within radiusPx of the (absolute-screen) mouse position, taken from the
        // last skeleton-overlay frame; -1 if none. Populated by RenderSkeleton; drives viewport bone picking.
        int PickBone( const ImVec2& absMouse, float radiusPx = 12.0f ) const;

        // True while the mouse is over a light billboard this frame — icons click-select the light,
        // so the scene ray-pick must not run over them (it would hit whatever is behind).
        bool IsLightIconHovered() const
        {
            return m_LightIconHovered;
        }

    private:
        void RenderPointLights( const std::shared_ptr<Desert::Core::Camera>& camera, float width, float height,
                                float xpos, float ypos );
        // Sun billboard + direction arrow (direction = -normalize(Translation), the shading convention).
        void RenderDirectionLights( const std::shared_ptr<Desert::Core::Camera>& camera, float width,
                                    float height );
        void RenderSpotLights( const std::shared_ptr<Desert::Core::Camera>& camera, float width, float height,
                               float xpos, float ypos );
        // Camera entities (CameraComponent): billboard icon + wireframe view frustum.
        void RenderCameras( const std::shared_ptr<Desert::Core::Camera>& camera, float width, float height,
                            float xpos, float ypos );
        // Skeleton Edit mode: draw the selected skinned mesh's bind-pose skeleton (bone heads + parent->child
        // links) as an overlay, with the bone-tree-selected bone highlighted. Read-only (Phase 1).
        void RenderSkeleton( const std::shared_ptr<Desert::Core::Camera>& camera, float width, float height,
                             float xpos, float ypos );
        // Billboard icons for entities with no rendered geometry (spawn points, audio emitters, triggers,
        // empties) so they are visible in the viewport. Hover shows a tooltip; the normal LMB pick selects them.
        void RenderSpawnIcons( const std::shared_ptr<Desert::Core::Camera>& camera, float width, float height );
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

        // (boneIndex, absolute-screen head position) captured each frame RenderSkeleton draws — the source
        // for PickBone. Cleared when Skeleton Edit mode is inactive so stale positions never pick.
        std::vector<std::pair<int, ImVec2>> m_BoneScreenPositions;

        bool m_LightIconHovered = false; // see IsLightIconHovered()
    };
} // namespace Desert::Editor
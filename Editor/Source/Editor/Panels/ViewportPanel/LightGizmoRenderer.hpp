#pragma once

#include <Engine/Desert.hpp>
#include <imgui/imgui.h>

#include <optional>
#include <string>
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

        // True while the mouse is over a light billboard OR a drag handle this frame — both consume the
        // click, so the scene ray-pick must not run under them (it would hit whatever is behind).
        bool IsLightIconHovered() const
        {
            return m_LightIconHovered;
        }

        // True while a radius / range / cone handle is being dragged. The viewport keeps its camera and
        // gizmo off during that (a handle drag is a value edit, not a selection or a move).
        bool IsDraggingHandle() const
        {
            return m_ActiveHandle != HandleKind::None;
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
        // In-editor rig placement (RigBuilder): overlay the bones being placed on a static mesh before
        // "Convert to Skinned". Shares the UE-style visuals with RenderSkeleton via DrawBoneGizmos.
        void RenderRigBuilder( const std::shared_ptr<Desert::Core::Camera>& camera, float width, float height );
        // Shared UE-style bone drawing: octahedral parent->child links + sphere joints, from already-projected
        // absolute-screen head positions (nullopt = behind camera). parents[i] < 0 marks a root. When
        // recordForPick is set, fills m_BoneScreenPositions for PickBone.
        void DrawBoneGizmos( ImDrawList* drawList, const std::vector<std::optional<ImVec2>>& screen,
                             const std::vector<int>& parents, const std::vector<std::string>& names,
                             int selectedBone, bool showAllNames, bool recordForPick );
        // Billboard icons for entities with no rendered geometry (spawn points, audio emitters, triggers,
        // empties) so they are visible in the viewport. Hover shows a tooltip; the normal LMB pick selects them.
        void RenderSpawnIcons( const std::shared_ptr<Desert::Core::Camera>& camera, float width, float height );
        // Text entities (TextComponent): a big, click-selectable "Aa" billboard so a label is easy to find
        // and grab in the viewport even when its glyphs are small/edge-on.
        void RenderTextIcons( const std::shared_ptr<Desert::Core::Camera>& camera, float width, float height );
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

        // --- draggable value handles (selected light only) ------------------------------------------
        // What a grab is currently editing. One at a time: a drag owns the mouse until it is released.
        enum class HandleKind
        {
            None,
            PointRadius,
            SpotRange,
            SpotOuterAngle,
        };

        // A round grab dot at @p handle. Dragging scales @p value by the RATIO of the pointer's distance
        // from @p center to that distance when the grab started, which keeps the feel identical at any
        // zoom or camera angle without needing a 3D ray intersection. Returns true when it wrote a value;
        // pushes one undo entry per completed drag.
        bool DragValueHandle( HandleKind kind, const Common::UUID& owner, const ImVec2& center,
                              const ImVec2& handle, float& value, float minValue, float maxValue,
                              const char* tooltip );

        // Is this the entity the Details panel is showing? Handles only appear on the selected light —
        // otherwise a scene full of lights would be a minefield of grab dots.
        bool IsSelected( const ECS::Entity& entity ) const;

    private:
        std::shared_ptr<Desert::Core::Scene> m_Scene;

        // (boneIndex, absolute-screen head position) captured each frame RenderSkeleton draws — the source
        // for PickBone. Cleared when Skeleton Edit mode is inactive so stale positions never pick.
        std::vector<std::pair<int, ImVec2>> m_BoneScreenPositions;

        bool m_LightIconHovered = false; // see IsLightIconHovered()

        // Active handle drag. The start value + start pixel distance define the proportional drag; the
        // captured bytes become one undo entry when the mouse is released.
        HandleKind   m_ActiveHandle = HandleKind::None;
        Common::UUID m_ActiveHandleOwner;
        float*       m_ActiveHandleTarget = nullptr; // the float being edited (undo target)
        float        m_DragStartValue     = 0.0f;
        float        m_DragStartDistance  = 0.0f;
    };
} // namespace Desert::Editor
#pragma once

#include <Editor/Panels/IPanel.hpp>
#include <Engine/ECS/Entity.hpp>
#include <vector>
#include <glm/glm.hpp>
#include <memory>

namespace Desert
{
    class DynamicMesh;
    namespace Core { class Scene; }
    namespace Graphic { class SceneRenderer; }
}

namespace Desert::Editor
{
    namespace UI { class UIHelper; }

    class MeshEditorPanel final : public IPanel
    {
    public:
        enum class EditorTool
        {
            Select,
            Move,
            Modify
        };

        struct Selection
        {
            std::vector<size_t> VertexIndices;
            glm::vec3           GetCenter( const ::Desert::DynamicMesh& mesh ) const;
            bool                Contains( size_t idx ) const;
            void                Toggle( size_t idx );
        };

        explicit MeshEditorPanel( std::shared_ptr<::Desert::Core::Scene> scene );
        ~MeshEditorPanel() override;

        void OnUIRender() override;

        void SetTarget( ECS::Entity entity );
        void ClearTarget();

        [[nodiscard]] std::shared_ptr<::Desert::DynamicMesh> GetActiveMesh() const { return m_Mesh; }
        [[nodiscard]] ECS::Entity                            GetTargetEntity() const { return m_TargetEntity; }

        // True while this panel is open and editing `entity` — the main viewport uses this to step aside
        // (suppress its object gizmo) so vertex editing owns the interaction (shared global ImGuizmo state).
        [[nodiscard]] bool IsActivelyEditing( ECS::Entity entity ) const
        {
            return m_SowPanel && m_Mesh && m_TargetEntity == entity;
        }
        [[nodiscard]] Selection&                             GetSelection() { return m_Selection; }
        [[nodiscard]] const Selection&                       GetSelection() const { return m_Selection; }

        static MeshEditorPanel* GetInstance() { return s_Instance; }

    private:
        // ---- UI sections ----
        void DrawToolbar();
        void DrawStatsBar();
        void DrawVertexList();
        void DrawPropertiesPanel();
        void DrawTopologyPanel();
        void DrawPreviewViewport();
        void DrawGizmo();

        // ---- Operations ----
        void  CommitMeshEdit();
        void  RecalcNormals();
        void  WeldVertices( float threshold );
        void  FlipNormals();
        void  SelectAll();
        void  ClearSelection();
        float ComputeOrbitDistance() const;
        void  ResetCamera();

        // Moves the selected vertices (and any coincident-but-split twins) by a local-space delta, so
        // seams in per-face-duplicated meshes don't tear open. Re-uploads and marks the preview dirty.
        void  MoveSelectedVerticesLocal( const glm::vec3& deltaLocal );
        // World transform of the edited entity (identity if it has none). Drives the transform-aware gizmo.
        glm::mat4 GetTargetModelMatrix() const;

        // ---- Preview scene ----
        void InitPreviewScene();
        void UpdatePreviewCamera();

        // ---- State ----
        std::shared_ptr<::Desert::Core::Scene>  m_Scene;
        ECS::Entity                             m_TargetEntity;
        std::shared_ptr<::Desert::DynamicMesh>  m_Mesh;
        Selection                               m_Selection;
        EditorTool                              m_CurrentTool = EditorTool::Select;

        // Vertex list UI state
        char    m_SearchBuf[128] = {};
        int     m_VertexPageOffset = 0;
        bool    m_AutoRecalcNormals = false;
        bool    m_ShowWireframe     = false;   // future use

        // Orbit camera
        float   m_CamYaw      =  0.5f;
        float   m_CamPitch    =  0.35f;
        float   m_CamDistance =  5.0f;
        glm::vec3 m_CamTarget = { 0.f, 0.f, 0.f };
        bool    m_OrbitDragging = false;
        glm::vec2 m_LastMousePos = {};

        // Pane split (fraction of total width)
        float m_SplitRatio = 0.32f;

        // Preview scene
        std::unique_ptr<Graphic::SceneRenderer> m_PreviewRenderer;
        std::shared_ptr<::Desert::Core::Scene>  m_PreviewScene;
        ECS::Entity                             m_PreviewEntity;
        ECS::Entity                             m_PreviewCamera;
        std::unique_ptr<UI::UIHelper>           m_UIHelper;

        // Render-on-demand: the preview scene is only re-rendered when something visible changed
        // (camera, mesh edit, resize, target/transform change). Otherwise the last frame is blitted.
        bool     m_PreviewDirty = true;
        uint32_t m_LastPreviewW = 0;
        uint32_t m_LastPreviewH = 0;

        // Viewport rect (set during RenderRightPane, used by DrawGizmo)
        float m_ViewportX = 0, m_ViewportY = 0, m_ViewportW = 1, m_ViewportH = 1;

        static inline MeshEditorPanel* s_Instance = nullptr;
    };
} // namespace Desert::Editor

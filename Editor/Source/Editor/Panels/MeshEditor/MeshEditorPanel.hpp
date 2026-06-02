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

        explicit MeshEditorPanel( std::shared_ptr<::Desert::Core::Scene> scene );
        ~MeshEditorPanel() override;

        void OnUIRender() override;
        
        void SetTarget( ECS::Entity entity );
        void ClearTarget();

        [[nodiscard]] std::shared_ptr<::Desert::DynamicMesh> GetActiveMesh() const { return m_Mesh; }
        [[nodiscard]] ECS::Entity                            GetTargetEntity() const { return m_TargetEntity; }
        
        struct Selection
        {
            std::vector<size_t> VertexIndices;
            glm::vec3           GetCenter( const ::Desert::DynamicMesh& mesh ) const;
        };

        [[nodiscard]] Selection&       GetSelection() { return m_Selection; }
        [[nodiscard]] const Selection& GetSelection() const { return m_Selection; }

        static MeshEditorPanel* GetInstance() { return s_Instance; }

    private:
        void RenderToolbar();
        void RenderLeftPane();
        void RenderRightPane();
        
        void InitPreviewScene();
        void UpdatePreviewScene();

    private:
        std::shared_ptr<::Desert::Core::Scene> m_Scene;
        ECS::Entity                            m_TargetEntity;
        std::shared_ptr<::Desert::DynamicMesh> m_Mesh;
        Selection                              m_Selection;
        
        EditorTool m_CurrentTool = EditorTool::Select;

        // Preview Scene Data
        std::unique_ptr<Graphic::SceneRenderer> m_PreviewRenderer;
        std::shared_ptr<::Desert::Core::Scene>  m_PreviewScene;
        ECS::Entity                             m_PreviewEntity;
        std::unique_ptr<UI::UIHelper>           m_UIHelper;

        static inline MeshEditorPanel* s_Instance = nullptr;
    };
} // namespace Desert::Editor

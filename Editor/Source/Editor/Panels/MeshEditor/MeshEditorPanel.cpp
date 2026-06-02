#include "MeshEditorPanel.hpp"
#include <ImGui/imgui.h>
#include <ImGuizmo.h>
#include <Common/Core/Math/Ray.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/StaticMesh.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/ECS/System/MeshECSSystem.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/EditorResources.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Widgets/UIHelper/ImGuiUI.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    MeshEditorPanel::MeshEditorPanel( std::shared_ptr<::Desert::Core::Scene> scene ) 
        : IPanel( "Mesh Editor" ), m_Scene( scene ), m_Mesh( nullptr ),
          m_UIHelper( std::make_unique<UI::UIHelper>() )
    {
        s_Instance = this;
        m_UIHelper->Init();
        m_SowPanel = false; // Hidden by default
    }

    MeshEditorPanel::~MeshEditorPanel()
    {
        if ( s_Instance == this )
            s_Instance = nullptr;
    }

    void MeshEditorPanel::OnUIRender()
    {
        if ( !m_TargetEntity || !m_Mesh )
        {
            ImGui::Text( "No entity selected for editing." );
            return;
        }

        // Toolbar at the top
        RenderToolbar();
        
        ImGui::Separator();

        // Main Layout: Left Pane (Data) and Right Pane (Visual)
        static float leftPaneWidth = 350.0f;
        
        ImGui::BeginChild( "LeftPane", ImVec2( leftPaneWidth, 0 ), true );
        RenderLeftPane();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild( "RightPane", ImVec2( 0, 0 ), true );
        RenderRightPane();
        ImGui::EndChild();
    }

    void MeshEditorPanel::RenderToolbar()
    {
        auto ButtonTool = [&]( EditorTool tool, const char* icon, const char* label )
        {
            bool active = m_CurrentTool == tool;
            if ( active ) ImGui::PushStyleColor( ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] );
            
            if ( ImGui::Button( icon ) )
                m_CurrentTool = tool;
            
            if ( active ) ImGui::PopStyleColor();
            
            if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "%s", label );
            ImGui::SameLine();
        };

        ButtonTool( EditorTool::Select, ICON_MDI_CURSOR_DEFAULT, "Select" );
        ButtonTool( EditorTool::Move,   ICON_MDI_AXIS_ARROW,    "Move Vertices" );
        ButtonTool( EditorTool::Modify, ICON_MDI_TUNE,          "Modify Properties" );

        ImGui::SameLine( ImGui::GetWindowWidth() - 150.0f );
        if ( ImGui::Button( "Flatten Mesh" ) )
        {
            m_Mesh->Flatten();
            m_Selection.VertexIndices.clear();
            UpdatePreviewScene();
        }
        
        ImGui::SameLine();
        if ( ImGui::Button( "Update GPU" ) )
        {
            m_Mesh->Invalidate();
            UpdatePreviewScene();
        }
    }

    void MeshEditorPanel::RenderLeftPane()
    {
        ImGui::TextColored( { 0.4f, 0.7f, 1.0f, 1.0f }, "Vertex Manager" );
        ImGui::Separator();

        if ( ImGui::BeginChild( "VertexList", ImVec2( 0, 300 ), true ) )
        {
            const auto& vertices = m_Mesh->GetVertices();
            for ( uint32_t i = 0; i < (uint32_t)vertices.size(); ++i )
            {
                char label[32];
                sprintf( label, "Vertex %u", i );
                
                bool isSelected = std::find( m_Selection.VertexIndices.begin(), 
                                           m_Selection.VertexIndices.end(), i ) != m_Selection.VertexIndices.end();
                
                if ( ImGui::Selectable( label, isSelected ) )
                {
                    if ( !ImGui::GetIO().KeyCtrl ) m_Selection.VertexIndices.clear();
                    if ( !isSelected ) m_Selection.VertexIndices.push_back( i );
                }
            }
        }
        ImGui::EndChild();

        if ( !m_Selection.VertexIndices.empty() )
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text( "Selection Details (%zu)", m_Selection.VertexIndices.size() );
            
            if ( m_Selection.VertexIndices.size() == 1 )
            {
                size_t idx = m_Selection.VertexIndices[0];
                auto& v = m_Mesh->GetVertices()[idx];
                
                bool changed = false;
                changed |= ImGui::DragFloat3( "Position", glm::value_ptr( v.Position ), 0.01f );
                changed |= ImGui::DragFloat3( "Normal",   glm::value_ptr( v.Normal ),   0.01f );
                changed |= ImGui::DragFloat2( "TexCoord", glm::value_ptr( v.TexCoord ), 0.01f );
                
                if ( changed )
                {
                    m_Mesh->Update( m_Mesh->GetVertices(), m_Mesh->GetIndices() );
                    UpdatePreviewScene();
                }
            }
            else
            {
                ImGui::TextDisabled( "Multiple vertices selected." );
                ImGui::TextDisabled( "Use Move Tool in Visual Editor." );
            }
        }
    }

    void MeshEditorPanel::RenderRightPane()
    {
        if ( !m_PreviewScene ) InitPreviewScene();

        ImVec2 size = ImGui::GetContentRegionAvail();
        if ( size.x > 0 && size.y > 0 )
        {
            m_PreviewScene->Resize( (uint32_t)size.x, (uint32_t)size.y );
            
            m_PreviewScene->BeginScene();
            m_PreviewScene->OnUpdate( Common::Timestep( 0.016f ) ); 
            m_PreviewScene->EndScene();

            auto finalImage = m_PreviewScene->GetFinalImage();
            m_UIHelper->Image( finalImage, size );

            // --- GIZMOS ---
            if ( m_CurrentTool == EditorTool::Move && !m_Selection.VertexIndices.empty() )
            {
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetRect( ImGui::GetWindowPos().x + ImGui::GetCursorPosX(), 
                                   ImGui::GetWindowPos().y + ImGui::GetCursorPosY(), size.x, size.y );

                const auto& mainCamera = m_PreviewScene->GetMainCamera().lock();
                if ( mainCamera )
                {
                    const auto& view = mainCamera->GetViewMatrix();
                    const auto& proj = mainCamera->GetProjectionMatrix();
                    
                    glm::vec3 localCenter = m_Selection.GetCenter( *m_Mesh );
                    glm::mat4 gizmoTransform = glm::translate( glm::mat4( 1.0f ), localCenter );

                    if ( ImGuizmo::Manipulate( &view[0][0], &proj[0][0], 
                                               ImGuizmo::TRANSLATE, ImGuizmo::LOCAL, &gizmoTransform[0][0] ) )
                    {
                        glm::vec3 newCenter, scale, skew;
                        glm::quat rotation;
                        glm::vec4 perspective;
                        glm::decompose( gizmoTransform, scale, rotation, newCenter, skew, perspective );

                        glm::vec3 delta = newCenter - localCenter;
                        auto& vertices = m_Mesh->GetVertices();
                        for ( auto idx : m_Selection.VertexIndices )
                            vertices[idx].Position += delta;

                        m_Mesh->Update( vertices, m_Mesh->GetIndices() );
                        UpdatePreviewScene();
                    }
                }
            }
        }
    }

    void MeshEditorPanel::InitPreviewScene()
    {
        m_PreviewRenderer = std::make_unique<Graphic::SceneRenderer>();
        m_PreviewScene = std::make_shared<::Desert::Core::Scene>( "MeshPreview", m_PreviewRenderer.get() );
        m_PreviewScene->Init();

        // Setup Camera
        auto& camEntity = m_PreviewScene->CreateNewEntity( "PreviewCamera" );
        auto& camComp = camEntity.AddComponent<ECS::CameraComponent>();
        camComp.IsMainCamera = true;
        
        auto& camTransform = camEntity.GetComponent<ECS::TransformComponent>();
        camTransform.Translation = { 0, 0, 3 };

        // Setup Light
        auto& lightEntity = m_PreviewScene->CreateNewEntity( "PreviewLight" );
        lightEntity.AddComponent<ECS::DirectionLightComponent>();
        lightEntity.GetComponent<ECS::TransformComponent>().Translation = { 1, 1, 1 };

        // Setup the Mesh we are editing
        m_PreviewEntity = m_PreviewScene->CreateNewEntity( "EditTarget" );
        auto& smc = m_PreviewEntity.AddComponent<ECS::StaticMeshComponent>();
        smc.RuntimeMesh = m_Mesh;
        
        m_PreviewScene->AddSystem<ECS::MeshECSSystem>();
    }

    void MeshEditorPanel::UpdatePreviewScene()
    {
        if ( m_PreviewEntity )
        {
            auto& smc = m_PreviewEntity.GetComponent<ECS::StaticMeshComponent>();
            smc.RuntimeMesh = m_Mesh;
        }
    }

    void MeshEditorPanel::SetTarget( ECS::Entity entity )
    {
        m_TargetEntity = entity;
        m_SowPanel = true; // Show window
        m_Selection.VertexIndices.clear();
        
        if ( m_TargetEntity.HasComponent<ECS::StaticMeshComponent>() )
        {
            auto& smc = m_TargetEntity.GetComponent<ECS::StaticMeshComponent>();
            if ( !smc.RuntimeMesh )
            {
                // Logic to clone/create runtime mesh
                if ( smc.Primitive.has_value() )
                {
                    auto dynamicMesh = Geometry::PrimitiveMeshFactory::Create( smc.Primitive.value() );
                    smc.RuntimeMesh = std::make_shared<DynamicMesh>( 
                        dynamicMesh->GetVertices(), dynamicMesh->GetIndices(), dynamicMesh->GetSubmeshes() 
                    );
                    smc.RuntimeMesh->Invalidate();
                }
                else if ( smc.MeshHandle )
                {
                    auto* asset = Runtime::ResourceRegistry::GetMeshService()->GetAsset( smc.MeshHandle );
                    if ( asset && !asset->IsSkinned() )
                    {
                         auto* staticAsset = static_cast<Assets::StaticMeshAsset*>( asset );
                         smc.RuntimeMesh = std::make_shared<DynamicMesh>( 
                             staticAsset->GetVertices(), staticAsset->GetIndices(), staticAsset->GetSubmeshes() 
                         );
                         smc.RuntimeMesh->Invalidate();
                    }
                }
            }
            m_Mesh = smc.RuntimeMesh;
        }
        
        // Re-init or update preview
        if ( m_PreviewScene ) UpdatePreviewScene();
    }

    void MeshEditorPanel::ClearTarget()
    {
        m_TargetEntity = {};
        m_Mesh = nullptr;
        m_Selection.VertexIndices.clear();
        m_SowPanel = false;
    }

    glm::vec3 MeshEditorPanel::Selection::GetCenter( const ::Desert::DynamicMesh& mesh ) const
    {
        if ( VertexIndices.empty() ) return glm::vec3( 0.0f );
        
        glm::vec3 center( 0.0f );
        for ( auto idx : VertexIndices )
        {
            center += mesh.GetVertices()[idx].Position;
        }
        return center / (float)VertexIndices.size();
    }

} // namespace Desert::Editor

#include "StaticMeshComponent.hpp"

#include <ImGui/imgui.h>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    StaticMeshComponentWidget::StaticMeshComponentWidget( const std::weak_ptr<Assets::AssetManager>& assetManager )
         : ComponentWidget( "3D Model" ), m_AssetManager( assetManager )
    {
    }

    void StaticMeshComponentWidget::Render( ECS::Entity& entity )
    {
        const auto assetManager = m_AssetManager.lock();
        auto       meshAssets   = assetManager->FindAllByType<Assets::MeshAsset>();

        auto&       staticMesh          = entity.GetComponent<ECS::StaticMeshComponent>();
        const auto& currentSelectedMesh = assetManager->FindByHandle<Assets::MeshAsset>( staticMesh.MeshHandle );
        std::string currentMeshName =
             currentSelectedMesh
                  ? Common::Utils::FileSystem::GetFileName( currentSelectedMesh->GetMetadata().Filepath )
                  : "None";

        if ( ImGui::Button( currentMeshName.c_str(), ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
        {
            ImGui::OpenPopup( "mesh_selector" );
        }

        if ( ImGui::BeginPopup( "mesh_selector" ) )
        {
            static ImGuiTextFilter meshFilter;
            meshFilter.Draw( "##Search", 200 );
            ImGui::Separator();

            for ( const auto& [handle, meshAsset] : meshAssets )
            {
                const std::string& meshName =
                     Common::Utils::FileSystem::GetFileName( meshAsset->GetMetadata().Filepath );
                if ( meshFilter.PassFilter( meshName.c_str() ) )
                {
                    bool isSelected = ( staticMesh.MeshHandle == handle );
                    if ( ImGui::Selectable( meshName.c_str(), isSelected ) )
                    {
                        staticMesh.MeshHandle = handle;
                    }

                    if ( isSelected )
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }

            if ( meshAssets.empty() )
            {
                ImGui::TextDisabled( "No mesh assets available" );
            }

            ImGui::EndPopup();
        }

        if ( ImGui::Checkbox( "Mesh Outline", &staticMesh.OutlineDraw) )
        {
        }

        // if ( staticMesh.Mesh )
        {
            /*ImGui::Dummy( ImVec2( 0, 10 ) );
            ImGui::Text( "Mesh Info:" );
            ImGui::Text( "Vertices: %zu", staticMesh.Mesh->GetVertexCount() );
            ImGui::Text( "Triangles: %zu", staticMesh.Mesh->GetTriangleCount() );*/
        }
    }

} // namespace Desert::Editor
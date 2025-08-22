#include "SkyboxComponent.hpp"

#include <ImGui/imgui.h>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    SkyboxComponentWidget::SkyboxComponentWidget( const std::weak_ptr<Assets::AssetManager>& assetManager )
         : ComponentWidget( "Skybox" ), m_AssetManager( assetManager )
    {
    }

    void SkyboxComponentWidget::Render( ECS::Entity& entity )
    {
        const auto assetManager = m_AssetManager.lock();
        auto       meshAssets   = assetManager->FindAllByType<Assets::SkyboxAsset>();

        auto&       skybox                = entity.GetComponent<ECS::SkyboxComponent>();
        const auto& currentSelectedSkybox = assetManager->FindByHandle<Assets::SkyboxAsset>( skybox.SkyboxHandle );
        std::string currentSkyboxName =
             currentSelectedSkybox
                  ? Common::Utils::FileSystem::GetFileName( currentSelectedSkybox->GetMetadata().Filepath )
                  : "None";

        if ( ImGui::Button( currentSkyboxName.c_str(), ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
        {
            ImGui::OpenPopup( "skybox_selector" );
        }

        if ( ImGui::BeginPopup( "skybox_selector" ) )
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
                    bool isSelected = ( skybox.SkyboxHandle == handle );
                    if ( ImGui::Selectable( meshName.c_str(), isSelected ) )
                    {
                        skybox.SkyboxHandle = handle;
                    }

                    if ( isSelected )
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }

            if ( meshAssets.empty() )
            {
                ImGui::TextDisabled( "No skybox assets available" );
            }

            ImGui::EndPopup();
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
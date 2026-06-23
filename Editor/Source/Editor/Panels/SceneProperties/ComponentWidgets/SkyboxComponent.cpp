#include "SkyboxComponent.hpp"

#include <ImGui/imgui.h>

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    SkyboxComponentWidget::SkyboxComponentWidget( const std::weak_ptr<Assets::AssetManager>& assetManager )
         : ComponentWidget( "Skybox" ), m_AssetManager( assetManager )
    {
    }

    void SkyboxComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        const auto assetManager = m_AssetManager.lock();
        auto       skyboxAssets = assetManager->FindAllByType<Assets::SkyboxAsset>();

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
            static ImGuiTextFilter skyboxFilter;
            skyboxFilter.Draw( "##Search", 200 );
            ImGui::Separator();

            for ( const auto& [handle, meshAsset] : skyboxAssets )
            {
                const std::string& skyboxName =
                     Common::Utils::FileSystem::GetFileName( meshAsset->GetMetadata().Filepath );
                if ( skyboxFilter.PassFilter( skyboxName.c_str() ) )
                {
                    bool isSelected = ( skybox.SkyboxHandle == handle );
                    if ( ImGui::Selectable( skyboxName.c_str(), isSelected ) )
                    {
                        if ( handle != skybox.SkyboxHandle )
                        {
                            auto& skyboxService = *Runtime::ResourceRegistry::GetSkyboxService();
                            if ( !skyboxService.Get( handle ) )
                            {
                                Graphic::Renderer::GetInstance().WaitDeviceIdle();
                                skyboxService.Register( meshAsset );
                            }
                            skybox.SkyboxHandle = handle;
                        }
                    }

                    if ( isSelected )
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }

            if ( skyboxAssets.empty() )
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
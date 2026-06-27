#include "SkyboxComponent.hpp"

#include <ImGui/imgui.h>

#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>
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

        ImGui::Dummy( ImVec2( 0, 8 ) );
        ImGui::Separator();
        ImGui::Text( "Procedural Sky" );

        // Engine-generated atmosphere (no HDR asset). The sun follows the scene's directional light.
        ImGui::Checkbox( "Procedural", &skybox.Procedural );
        ImGui::BeginDisabled( !skybox.Procedural );
        ImGui::SliderFloat( "Sun Intensity", &skybox.SunIntensity, 1.0f, 50.0f );
        ImGui::SliderFloat( "Sun Disk Size", &skybox.SunDiskRadius, 0.002f, 0.1f, "%.3f" );

        // Sky IBL is baked once when Procedural is first enabled; moving the sun does NOT auto-rebake
        // (it's a heavy device-idle operation). Press Bake to rebuild ambient/reflections from the
        // current sun direction + parameters.
        if ( ImGui::Button( "Bake Sky IBL", ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
            skybox.RequestBake = true;

        // Engine-generated volumetric clouds (raymarched in the sky pass; visual only — not baked into IBL).
        ImGui::Dummy( ImVec2( 0, 6 ) );
        ImGui::Separator();
        ImGui::Checkbox( "Volumetric Clouds", &skybox.EnableClouds );
        ImGui::BeginDisabled( !skybox.EnableClouds );
        ImGui::SliderFloat( "Coverage", &skybox.CloudCoverage, 0.0f, 1.0f );
        ImGui::SliderFloat( "Density", &skybox.CloudDensity, 0.0f, 3.0f );
        ImGui::SliderFloat( "Cloud Height", &skybox.CloudHeight, 100.0f, 3000.0f );
        ImGui::SliderFloat( "Thickness", &skybox.CloudThickness, 100.0f, 2000.0f );
        ImGui::SliderFloat( "Wind Speed", &skybox.CloudWindSpeed, 0.0f, 50.0f );
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        if ( skybox.Procedural )
            ImGui::TextDisabled( "HDR cubemap above is ignored while Procedural is on." );
    }

    DESERT_REGISTER_CUSTOM_COMPONENT(
         ECS::SkyboxComponent, "Skybox", false,
         ( []( ECS::Entity& e, ::Desert::Core::Scene* s, const ComponentEditContext& ctx )
           { SkyboxComponentWidget( ctx.AssetManager ).Render( e, s ); } ) )

} // namespace Desert::Editor
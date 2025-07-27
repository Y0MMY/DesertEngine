#include "ScenePropertiesPanel.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Widgets/Controls/Controls.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <Engine/Graphic/Materials/MaterialFactory.hpp>

#include <ImGui/imgui.h>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    void ScenePropertiesPanel::OnUIRender()
    {
        if ( const auto& resolver = m_RuntimeResourceResolver.lock() )
        {
            ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 10, 8 ) );
            ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 4.0f );
            ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 3.0f );
            ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8, 6 ) );
            ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.15f, 0.15f, 0.17f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( 0.08f, 0.08f, 0.10f, 1.0f ) );

            ImGui::Begin( m_PanelName.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar );

            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.9f, 0.9f, 0.9f, 1.0f ) );
            ImGui::TextUnformatted( "Entity Properties" );
            ImGui::PopStyleColor();

            ImGui::Separator();
            ImGui::Dummy( ImVec2( 0, 6 ) );

            auto selectedOpt = Core::SelectionManager::GetSelected();

            if ( selectedOpt )
            {
                const auto& selectedEntityOpt = m_Scene->FindEntityByID( selectedOpt.value() );
                if ( selectedEntityOpt )
                {
                    const auto& selectedEntity = selectedEntityOpt.value().get();

                    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.7f, 0.7f, 0.8f, 0.9f ) );
                    if ( selectedEntity.HasComponent<ECS::UUIDComponent>() )
                        ImGui::Text( "ID: %s",
                                     selectedEntity.GetComponent<ECS::UUIDComponent>().UUID.ToString().c_str() );
                    ImGui::PopStyleColor();

                    ImGui::Dummy( ImVec2( 0, 8 ) );

                    if ( selectedEntity.HasComponent<ECS::TagComponent>() )
                    {
                        auto tag = &selectedEntity.GetComponent<ECS::TagComponent>().Tag[0];

                        ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.11f, 0.11f, 0.12f, 1.0f ) );
                        ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( 0.13f, 0.13f, 0.14f, 1.0f ) );
                        ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( 0.09f, 0.09f, 0.10f, 1.0f ) );
                        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.95f, 0.95f, 0.95f, 1.0f ) );
                        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 1.0f );
                        ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( 0.2f, 0.2f, 0.25f, 1.0f ) );

                        ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x );
                        if ( ImGui::InputText( "##EntityName", tag,
                                               ImGuiInputTextFlags_EnterReturnsTrue |
                                                    ImGuiInputTextFlags_AutoSelectAll ) )
                        {
                        }

                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor( 5 );

                        ImGui::Dummy( ImVec2( 0, 10 ) );
                    }

                    if ( selectedEntity.HasComponent<ECS::SkyboxComponent>() )
                    {

                        auto& skyboxComponent = selectedEntity.GetComponent<ECS::SkyboxComponent>();
                        const std::shared_ptr<Graphic::MaterialSkybox> skyboxInstance =
                             resolver->ResolveSkybox( skyboxComponent.SkyboxHandle );

                        if ( ImGui::CollapsingHeader( "Skybox", ImGuiTreeNodeFlags_DefaultOpen ) )
                        {
                            ImGui::Dummy( ImVec2( 0, 4 ) );
                            auto& skyboxComponent = selectedEntity.GetComponent<ECS::SkyboxComponent>();

                            const char* skyboxTypes[] = { "Cubemap", "Procedural", "Gradient" };
                            static int  currentType   = 0;
                            ImGui::Combo( "Type", &currentType, skyboxTypes, IM_ARRAYSIZE( skyboxTypes ) );

                            if ( currentType == 0 ) // Cubemap
                            {
                                ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.8f, 0.8f, 0.85f, 1.0f ) );
                                ImGui::Text( "Current Cubemap:" );
                                ImGui::PopStyleColor();

                                ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.7f, 0.7f, 0.75f, 1.0f ) );

                                std::string path = "None";
                                /*if ( skyboxInstance && skyboxInstance->IsReady() )
                                {
                                    auto skyboxAsset = skyboxInstance->GetBaseMaterial();
                                    if ( skyboxAsset && !skyboxAsset->GetBaseFilepath().empty() )
                                    {
                                        path = skyboxAsset->GetBaseFilepath().string();
                                    }
                                }*/

                                ImGui::TextWrapped( "%s", path.c_str() );
                                ImGui::PopStyleColor();

                                ImGui::Dummy( ImVec2( 0, 6 ) );

                                if ( ImGui::Button( "Load Cubemap",
                                                    ImVec2( ImGui::GetContentRegionAvail().x, 24 ) ) )
                                {
                                    auto path = Common::Utils::FileSystem::OpenFileDialog(
                                         "Cubemap Files (*.hdr;*.exr)\0*.hdr;*.exr\0All Files (*.*)\0*.*\0" );
                                    if ( !path.empty() )
                                    {

                                        // TODO:
                                    }
                                }
                            }
                            else if ( currentType == 1 ) // Procedural
                            {
                                // Widgets::DrawFloatControl( "Sun Size", skyboxComponent.SunSize, 0.01f,
                                // 0.0f, 1.0f ); Widgets::DrawFloatControl( "Sun Power", skyboxComponent.SunPower,
                                // 0.1f, 0.0f, 10.0f
                                // ); Widgets::DrawFloatControl( "Exposure", skyboxComponent.Exposure, 0.1f,
                                // 0.0f, 5.0f
                                // );
                            }
                            else if ( currentType == 2 ) // Gradient
                            {
                                // Widgets::DrawColorControl( "Top Color", skyboxComponent.TopColor );
                                // Widgets::DrawColorControl( "Middle Color", skyboxComponent.MiddleColor );
                                // Widgets::DrawColorControl( "Bottom Color", skyboxComponent.BottomColor );
                            }

                            ImGui::Dummy( ImVec2( 0, 10 ) );
                            // Widgets::DrawFloatControl( "Intensity", skyboxComponent.Intensity, 0.05f, 0.0f, 5.0f
                            // ); Widgets::DrawToggleControl( "Enable Rotation", skyboxComponent.RotationEnabled );
                            // if ( skyboxComponent.RotationEnabled )
                            //{
                            //     Widgets::DrawFloatControl( "Rotation Speed", skyboxComponent.RotationSpeed,
                            //     0.1f,
                            //                                -5.0f, 5.0f );
                            // }
                        }
                    }

                    if ( selectedEntity.HasComponent<ECS::StaticMeshComponent>() )
                    {
                        auto&      meshComponent = selectedEntity.GetComponent<ECS::StaticMeshComponent>();
                        const auto mesh          = resolver->ResolveMesh( meshComponent.MeshHandle );

                        if ( ImGui::CollapsingHeader( "Static Mesh", ImGuiTreeNodeFlags_DefaultOpen ) )
                        {
                            ImGui::Dummy( ImVec2( 0, 4 ) );

                            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.8f, 0.8f, 0.85f, 1.0f ) );
                            ImGui::Text( "Current Mesh:" );
                            ImGui::PopStyleColor();

                            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.7f, 0.7f, 0.75f, 1.0f ) );

                            std::string currentMesh = "None";
                            if ( mesh )
                            {
                                currentMesh = mesh->GetFilepath();
                            }

                            ImGui::TextWrapped( "%s", currentMesh.c_str() );
                            ImGui::PopStyleColor();

                            ImGui::Dummy( ImVec2( 0, 6 ) );
                        }

                        if ( ImGui::BeginCombo( "##MeshSelector", "Select Mesh" ) )
                        {
                            auto meshAssets = m_AssetManager->FindAllByType<Assets::MeshAsset>();
                            for ( const auto& [handle, asset] : meshAssets )
                            {
                                bool isSelected = ( handle == meshComponent.MeshHandle );
                                if ( ImGui::Selectable(
                                          Common::Utils::FileSystem::GetFileName( asset->GetMesh()->GetFilepath() )
                                               .c_str(),
                                          isSelected ) )
                                {
                                    meshComponent.MeshHandle = handle;

                                    const auto base = m_AssetManager->CreateAsset<Assets::MaterialAsset>(
                                         Assets::AssetPriority::Low, asset->GetMesh()->GetFilepath() );

                                    meshComponent.Material.reset();
                                    meshComponent.Material = std::make_shared<Graphic::MaterialPBR>( base );
                                }
                                if ( isSelected )
                                {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }

                        DrawMaterialEntity( selectedEntity );
                    }

                    if ( selectedEntity.HasComponent<ECS::TransformComponent>() &&
                         !selectedEntity.HasComponent<ECS::DirectionLightComponent>() )
                    {
                        if ( ImGui::CollapsingHeader( "Transform", ImGuiTreeNodeFlags_DefaultOpen ) )
                        {
                            ImGui::Dummy( ImVec2( 0, 4 ) );
                            auto& transform = selectedEntity.GetComponent<ECS::TransformComponent>();

                            Widgets::DrawVec3Control( "Position", transform.Translation );
                            ImGui::Dummy( ImVec2( 0, 6 ) );
                            Widgets::DrawVec3Control( "Rotation", transform.Rotation );
                            ImGui::Dummy( ImVec2( 0, 6 ) );
                            Widgets::DrawVec3Control( "Scale", transform.Scale, 1.0f );
                        }
                    }
                    else
                    {
                        if ( ImGui::CollapsingHeader( "Direction", ImGuiTreeNodeFlags_DefaultOpen ) )
                        {
                            ImGui::Dummy( ImVec2( 0, 4 ) );
                            auto& transform = selectedEntity.GetComponent<ECS::TransformComponent>();

                            Widgets::DrawDirectionWidget( "Direction", transform.Translation );
                        }

                        ImGui::Dummy( ImVec2( 0, 3 ) );

                        if ( ImGui::CollapsingHeader( "Attributes", ImGuiTreeNodeFlags_DefaultOpen ) )
                        {
                            ImGui::Dummy( ImVec2( 0, 4 ) );
                            auto& transform = selectedEntity.GetComponent<ECS::TransformComponent>();

                            ///      Widgets::DrawLightColorControl("Light", transform.Position);
                            //  Widgets::DrawLightIntensityControl("Intensity", transform.Position.x);
                        }
                    }
                }
            }
            else
            {
                ImGui::Dummy( ImVec2( 0, 20 ) );
                ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.6f, 0.6f, 0.65f, 0.8f ) );
                ImGui::SetCursorPosX( ( ImGui::GetWindowWidth() - ImGui::CalcTextSize( "No entity selected" ).x ) *
                                      0.5f );
                ImGui::Text( "No entity selected" );
                ImGui::PopStyleColor();
            }

            ImGui::End();
            ImGui::PopStyleColor( 2 );
            ImGui::PopStyleVar( 4 );
        }
    }

    void ScenePropertiesPanel::DrawMaterialEntity( const ECS::Entity& entity )
    {
        m_MaterialsPanel->DrawMaterialEntity( entity );
    }

} // namespace Desert::Editor
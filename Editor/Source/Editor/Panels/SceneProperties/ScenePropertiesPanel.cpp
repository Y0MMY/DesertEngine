#include "ScenePropertiesPanel.hpp"
#include "ComponentEditor.hpp"

#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/EditorResources.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Core/ThemeManager.hpp>
#include <Editor/Widgets/Controls/Controls.hpp>
#include <ImGui/imgui.h>
#include <Editor/Widgets/ThumbnailCache.hpp>
#include <Engine/Assets/Prefab/PrefabAsset.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>

#include <filesystem>
#include <system_error>
#include <Engine/Core/Scene.hpp>
#include <Common/Core/Constants.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
        const char* GetPrimaryComponentName( const ECS::Entity& entity )
        {
            if ( entity.HasComponent<ECS::CameraComponent>() )
                return "CameraComponent";
            if ( entity.HasComponent<ECS::DirectionLightComponent>() )
                return "DirectionalLightComponent";
            if ( entity.HasComponent<ECS::PointLightComponent>() )
                return "PointLightComponent";
            if ( entity.HasComponent<ECS::SpotLightComponent>() )
                return "SpotLightComponent";
            if ( entity.HasComponent<ECS::SkyboxComponent>() )
                return "SkyboxComponent";
            if ( entity.HasComponent<ECS::TerrainComponent>() )
                return "TerrainComponent";
            if ( entity.HasComponent<ECS::SkinnedMeshComponent>() )
                return "SkinnedMeshComponent";
            if ( entity.HasComponent<ECS::StaticMeshComponent>() )
                return "StaticMeshComponent";
            return "Actor";
        }

        const char* GetEntityIcon( const ECS::Entity& entity )
        {
            if ( entity.HasComponent<ECS::CameraComponent>() )
                return ICON_MDI_CAMERA;
            if ( entity.HasComponent<ECS::SpotLightComponent>() )
                return ICON_MDI_SPOTLIGHT;
            if ( entity.HasComponent<ECS::DirectionLightComponent>() ||
                 entity.HasComponent<ECS::PointLightComponent>() )
                return ICON_MDI_LIGHTBULB;
            if ( entity.HasComponent<ECS::SkyboxComponent>() )
                return ICON_MDI_EARTH;
            if ( entity.HasComponent<ECS::TerrainComponent>() )
                return ICON_MDI_TERRAIN;
            if ( entity.HasComponent<ECS::TextComponent>() )
                return ICON_MDI_FORMAT_TEXT;
            return ICON_MDI_CUBE_OUTLINE;
        }
    } // namespace

    void ScenePropertiesPanel::OnPreUpdate()
    {
        // Nothing GPU-side here any more. Details used to drive its own SceneRenderer for a live preview;
        // the engine writes per-frame scene state (camera, lights, shadow cascades) into the SHARED parent
        // material of the shader, so a second renderer stamped over what the viewport had just written and
        // the viewport lost its shadows. The 3D Model row shows the asset browser's cached thumbnail
        // instead — see StaticMeshComponentWidget::DrawMeshThumbnail.
    }

    void ScenePropertiesPanel::DrawSearchBox()
    {
        // UE's Details search: type a property name and the panel narrows to it (fields, categories and
        // whole components). Lives ABOVE the scrolling list so it never scrolls out of reach.
        ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetIconColor() );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( ICON_MDI_MAGNIFY );
        ImGui::PopStyleColor();
        ImGui::SameLine();

        const bool hasText = !m_FieldSearch.empty();
        ImGui::PushItemWidth( ImGui::GetContentRegionAvail().x -
                              ( hasText ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f ) );
        Utils::ImGuiUtilities::InputText( m_FieldSearch, "##DetailsSearch" );
        ImGui::PopItemWidth();

        if ( hasText )
        {
            ImGui::SameLine();
            if ( ImGui::Button( ICON_MDI_CLOSE "##ClearDetailsSearch" ) )
                m_FieldSearch.clear();
            Utils::ImGuiUtilities::Tooltip( "Clear the search" );
        }

        ImGui::Spacing();
    }

    void ScenePropertiesPanel::OnUIRender()
    {
        auto selectedOpt = Core::SelectionManager::GetSelected();
        if ( !selectedOpt.has_value() )
            return;

        const auto& selectedEntityOpt = m_Scene->FindEntityByID( selectedOpt.value() );
        if ( !selectedEntityOpt )
            return;

        const auto& selectedEntity = selectedEntityOpt.value().get();

        // --- Header row: [ ] [icon] [Name (bold, editable)]      [save] [tune] ---
        // Visible toggle -> VisibilityComponent.Visible (added on demand). Render systems skip invisible.
        bool active = !selectedEntity.HasComponent<ECS::VisibilityComponent>() ||
                      selectedEntity.GetComponent<ECS::VisibilityComponent>().Visible;
        if ( ImGui::Checkbox( "##ActiveCheckbox", &active ) )
        {
            // Cascade visibility to the whole subtree so hiding a parent hides its children (UE-like).
            m_Scene->SetVisibleRecursive( const_cast<ECS::Entity&>( selectedEntity ), active );
        }
        ImGui::SameLine();

        ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetIconColor() );
        ImGui::TextUnformatted( GetEntityIcon( selectedEntity ) );
        ImGui::PopStyleColor();
        ImGui::SameLine();

        auto& tag = selectedEntity.GetComponent<ECS::TagComponent>().Tag;

        ImGui::PushItemWidth( ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 4.5f );
        ImGui::PushFont( EditorResources::GetBoldFont() );
        if ( Utils::ImGuiUtilities::InputText( tag, "##InspectorNameChange" ) )
        {
        }
        ImGui::PopFont();
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.7f, 0.7f, 0.7f, 0.0f ) );

        if ( ImGui::Button( ICON_MDI_FLOPPY ) )
            ImGui::OpenPopup( "SavePrefab" );

        Utils::ImGuiUtilities::Tooltip( "Save Entity As Prefab" );

        ImGui::SameLine();
        if ( ImGui::Button( ICON_MDI_TUNE ) )
            ImGui::OpenPopup( "SetDebugMode" );

        ImGui::PopStyleColor();

        // --- Subtitle: component type ---
        {
            ImGui::SetCursorPosX( ImGui::GetFontSize() * 2.8f );
            ImGui::TextDisabled( "%s", GetPrimaryComponentName( selectedEntity ) );
        }

        if ( ImGui::BeginPopup( "SetDebugMode", 3 ) )
        {
            const bool hasPrefab = selectedEntity.HasComponent<ECS::PrefabComponent>();
            if ( !hasPrefab )
                ImGui::BeginDisabled();

            if ( ImGui::Button( "Revert To Prefab" ) )
            {
                if ( hasPrefab )
                {
                    auto& pc         = selectedEntity.GetComponent<ECS::PrefabComponent>();
                    auto  prefabAsset = m_AssetManager->FindByHandle<Assets::PrefabAsset>( pc.Prefab );
                    if ( prefabAsset )
                    {
                        std::optional<glm::vec3> savedPos;
                        if ( selectedEntity.HasComponent<ECS::TransformComponent>() )
                            savedPos = selectedEntity.GetComponent<ECS::TransformComponent>().Translation;

                        m_Scene->DestroyEntity( const_cast<ECS::Entity&>( selectedEntity ) );
                        Core::SelectionManager::ClearSelection();
                        prefabAsset->Instantiate( m_Scene.get(), *m_AssetManager, savedPos ? &*savedPos : nullptr );
                    }
                }
                ImGui::CloseCurrentPopup();
            }

            if ( !hasPrefab )
                ImGui::EndDisabled();

            ImGui::Separator();

            if ( ImGui::Selectable( "Debug Mode", m_DebugMode ) )
            {
                m_DebugMode = !m_DebugMode;
            }
            ImGui::EndPopup();
        }

        if ( ImGui::BeginPopupModal( "SavePrefab", NULL, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::Text( "Save Current Entity as a Prefab?\n\n" );
            ImGui::Separator();

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( "Name : " );
            ImGui::SameLine();
            Utils::ImGuiUtilities::InputText( tag, "##PrefabNameChange" );

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( "Path : " );
            ImGui::SameLine();
            Utils::ImGuiUtilities::InputText( m_PrefabSavePath, "##PrefabPathChange" );

            if ( ImGui::Button( "OK", ImVec2( 120, 0 ) ) )
            {
                const Common::Filepath fullPath =
                     Common::Filepath( m_PrefabSavePath ) / ( tag + Common::Constants::Extensions::PREFAB_EXTENSION );

                // Register in AssetManager (skip Load — we populate via CreateFromEntity)
                auto newPrefab = m_AssetManager->CreateAsset<Assets::PrefabAsset>(
                    Assets::AssetPriority::High, fullPath, false );

                if ( newPrefab )
                {
                    newPrefab->CreateFromEntity( const_cast<ECS::Entity&>( selectedEntity ), *m_AssetManager );

                    const std::string serialized = newPrefab->Serialize();
                    Common::Utils::FileSystem::WriteContentToFile( fullPath, serialized );

                    // Tag the source entity as a prefab instance so the hierarchy panel shows it
                    auto& sourceEntity = const_cast<ECS::Entity&>( selectedEntity );
                    auto& pc = sourceEntity.HasComponent<ECS::PrefabComponent>()
                        ? sourceEntity.GetComponent<ECS::PrefabComponent>()
                        : sourceEntity.AddComponent<ECS::PrefabComponent>();
                    pc.Prefab = newPrefab->GetMetadata().Handle;

                    LOG_INFO( "Prefab saved: {0}", fullPath.string() );
                }
                else
                {
                    LOG_ERROR( "Failed to create prefab asset at: {0}", fullPath.string() );
                }

                ImGui::CloseCurrentPopup();
            }

            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();

        if ( m_DebugMode )
        {
            // The entity's real UUID — this row used to print the TAG under a "UUID" label, which is the
            // one thing debug mode exists to show.
            Utils::ImGuiUtilities::ResetPropertyRows();
            Utils::ImGuiUtilities::BeginPropertyRow( "UUID", "Stable identity used by prefabs, sockets and "
                                                             "scripts" );
            ImGui::AlignTextToFramePadding();
            if ( selectedEntity.HasComponent<ECS::UUIDComponent>() )
            {
                ImGui::Text( "%llu", static_cast<unsigned long long>(
                                          selectedEntity.GetComponent<ECS::UUIDComponent>().UUID ) );
            }
            else
            {
                ImGui::TextDisabled( "none" );
            }
            Utils::ImGuiUtilities::EndPropertyRow();
        }

        DrawSearchBox();

        ImGui::BeginChild( "Components", ImVec2( 0.0f, 0.0f ), false, ImGuiWindowFlags_None );
        if ( !m_ComponentEditor )
            m_ComponentEditor = std::make_unique<ComponentEditor>( m_AssetManager, m_AnimationLibrary );
        // The texture-id cache the component rows blit their thumbnails through (lazy: an entity with no
        // thumbnail never builds one).
        if ( !m_ThumbnailUI )
        {
            m_ThumbnailUI = std::make_unique<UI::UIHelper>();
            m_ThumbnailUI->Init();
        }
        m_ComponentEditor->SetThumbnailUI( m_ThumbnailUI.get() );
        m_ComponentEditor->Render( const_cast<ECS::Entity&>( selectedEntity ), m_Scene.get(),
                                   m_FieldSearch.c_str() );
        ImGui::EndChild();
    }

} // namespace Desert::Editor
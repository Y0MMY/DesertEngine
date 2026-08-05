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
#include <Engine/Assets/Prefab/PrefabAsset.hpp>
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

    namespace
    {
        // Identity of what the preview shows: entity + mesh/primitive + material slots. A change re-points
        // (and re-frames) the preview; editing a material's own values does NOT change the key, because the
        // preview renders the live material instance and updates on its own.
        uint64_t PreviewKeyOf( const ECS::Entity& entity, uint64_t entityId )
        {
            if ( !entity.HasComponent<ECS::StaticMeshComponent>() )
                return 0;

            const auto& smc     = entity.GetComponent<ECS::StaticMeshComponent>();
            const bool  hasMesh = static_cast<uint64_t>( smc.MeshHandle ) != 0;
            if ( !hasMesh && !smc.Primitive )
                return 0;

            uint64_t   key = entityId | 1ull; // never 0 (0 means "nothing to preview")
            const auto mix = [&key]( uint64_t v )
            {
                key = key * 1099511628211ull ^ v; // FNV-style; only equality matters here
            };
            mix( static_cast<uint64_t>( smc.MeshHandle ) );
            mix( smc.Primitive ? static_cast<uint64_t>( *smc.Primitive ) + 1 : 0 );
            for ( const auto& slot : smc.MaterialSlots )
                mix( static_cast<uint64_t>( slot ) );
            return key ? key : 1ull;
        }
    } // namespace

    void ScenePropertiesPanel::OnPreUpdate()
    {
        // Everything GPU-side for the preview happens HERE: recording a scene render from inside
        // OnUIRender() destroys descriptor pools whose sets are bound to the frame's command buffer.
        if ( !m_SowPanel || !m_Scene )
            return;

        const auto selectedOpt = Core::SelectionManager::GetSelected();
        if ( !selectedOpt )
        {
            m_PreviewKey = 0;
            m_Preview.Clear();
            return;
        }

        const auto& entityOpt = m_Scene->FindEntityByID( *selectedOpt );
        if ( !entityOpt )
        {
            m_PreviewKey = 0;
            m_Preview.Clear();
            return;
        }

        const auto&    entity = entityOpt->get();
        const uint64_t key    = PreviewKeyOf( entity, static_cast<uint64_t>( *selectedOpt ) );
        if ( key != m_PreviewKey )
        {
            m_PreviewKey = key;
            if ( key == 0 )
            {
                m_Preview.Clear();
            }
            else
            {
                const auto& smc = entity.GetComponent<ECS::StaticMeshComponent>();
                if ( static_cast<uint64_t>( smc.MeshHandle ) != 0 )
                {
                    m_Preview.SetMesh( smc.MeshHandle, smc.MaterialSlots );
                }
                else
                {
                    // A primitive: preview the shape itself with its own material, not a stand-in sphere.
                    const auto shape = *smc.Primitive == Geometry::PrimitiveType::Plane
                                            ? PreviewViewport::Shape::Plane
                                            : ( *smc.Primitive == Geometry::PrimitiveType::Sphere
                                                     ? PreviewViewport::Shape::Sphere
                                                     : PreviewViewport::Shape::Cube );
                    m_Preview.SetMaterial( smc.MaterialSlots.empty()
                                                ? Assets::AssetHandle( static_cast<uint64_t>( 0 ) )
                                                : smc.MaterialSlots.front(),
                                           shape );
                }
            }
        }

        // Only pay for the render while the section is actually on screen and expanded. The flag is
        // consumed here and must be re-affirmed by every UI frame, so a collapsed window, a hidden dock tab
        // or a folded section all stop the render by simply not drawing.
        if ( m_PreviewActive && m_PreviewWidth > 0 && m_PreviewHeight > 0 )
            m_Preview.Update( m_PreviewWidth, m_PreviewHeight );
        m_PreviewActive = false;
    }

    void ScenePropertiesPanel::DrawPreviewSection()
    {
        if ( m_PreviewKey == 0 )
        {
            m_PreviewActive = false;
            return;
        }

        ImGui::SetNextItemOpen( m_PreviewOpen, ImGuiCond_Always );
        m_PreviewOpen = ImGui::CollapsingHeader( ICON_MDI_CUBE_SCAN " Preview" );
        if ( !m_PreviewOpen )
        {
            m_PreviewActive = false;
            return;
        }

        if ( !m_PreviewUI )
        {
            m_PreviewUI = std::make_unique<UI::UIHelper>();
            m_PreviewUI->Init();
        }

        const float width  = std::max( ImGui::GetContentRegionAvail().x, 32.0f );
        const float height = std::max( width / m_PreviewAspect, 96.0f );
        m_Preview.Draw( *m_PreviewUI, ImVec2( width, height ) );

        // Size for the NEXT frame's offscreen render (which happens in OnPreUpdate, before this runs again).
        // Quantized: a resize idles the GPU and recreates framebuffers, so dragging the dock splitter must
        // not do that on every pixel. The image is stretched over the remainder, which is under 16px.
        constexpr uint32_t kSizeStep = 16;
        m_PreviewWidth  = std::max<uint32_t>( static_cast<uint32_t>( width ) / kSizeStep, 2 ) * kSizeStep;
        m_PreviewHeight = std::max<uint32_t>( static_cast<uint32_t>( height ) / kSizeStep, 2 ) * kSizeStep;
        m_PreviewActive = true;

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
            ImGui::Columns( 2 );
            Utils::ImGuiUtilities::Property( "UUID", tag, Utils::ImGuiUtilities::PropertyFlag::ReadOnly );

            ImGui::Columns( 1 );
            ImGui::Separator();
        }

        // Above the (scrolling) component list, so orbiting the preview never fights the list's scroll and
        // the preview stays put while you edit fields below it.
        DrawPreviewSection();

        ImGui::BeginChild( "Components", ImVec2( 0.0f, 0.0f ), false, ImGuiWindowFlags_None );
        static ComponentEditor componentEditor( m_AssetManager, m_AnimationLibrary );
        componentEditor.Render( const_cast<ECS::Entity&>( selectedEntity ), m_Scene.get() );
        ImGui::EndChild();
    }

} // namespace Desert::Editor
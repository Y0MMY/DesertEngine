#include "PrefabComponentWidget.hpp"

#include <ImGui/imgui.h>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>

#include <Engine/Assets/Prefab/PrefabAsset.hpp>
#include <Common/Utilities/FileSystem.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    PrefabComponentWidget::PrefabComponentWidget( const Assets::AssetManager* assetManager )
         : ComponentWidget( "Prefab" ), m_AssetManager( assetManager )
    {
    }

    void PrefabComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        auto& prefab = entity.GetComponent<ECS::PrefabComponent>();

        Utils::ImGuiUtilities::PushID();
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );

        ImGui::Columns( 2 );
        ImGui::Separator();

        // ── Prefab name / handle ──────────────────────────────────────────
        std::shared_ptr<Assets::PrefabAsset> asset;
        std::string                          displayName = "<None>";
        bool                                 valid       = false;

        if ( m_AssetManager && prefab.Prefab )
        {
            asset = m_AssetManager->FindByHandle<Assets::PrefabAsset>( prefab.Prefab );
            if ( asset )
            {
                displayName = asset->GetMetadata().Filepath.filename().string();
                valid       = true;
            }
            else
            {
                displayName = "Missing (" + std::to_string( prefab.Prefab ) + ")";
            }
        }

        Utils::ImGuiUtilities::Property( "Asset", displayName, Utils::ImGuiUtilities::PropertyFlag::ReadOnly );

        // ── Status ───────────────────────────────────────────────────────
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( "Status" );
        ImGui::NextColumn();

        if ( valid )
            ImGui::TextColored( ImVec4( 0.2f, 0.9f, 0.3f, 1.f ), ICON_MDI_CHECK " Valid" );
        else if ( prefab.Prefab )
            ImGui::TextColored( ImVec4( 0.9f, 0.2f, 0.2f, 1.f ), ICON_MDI_ALERT " Missing" );
        else
            ImGui::TextColored( ImVec4( 0.6f, 0.6f, 0.6f, 1.f ), ICON_MDI_MINUS " None" );

        ImGui::NextColumn();
        ImGui::Columns( 1 );
        ImGui::Separator();

        // ── Select prefab by path ─────────────────────────────────────────
        ImGui::TextUnformatted( "Override Path" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - 60.0f );
        Utils::ImGuiUtilities::InputText( m_SelectPathBuf, "##PrefabSelectPath" );
        ImGui::SameLine();
        if ( ImGui::Button( "Load" ) && !m_SelectPathBuf.empty() )
        {
            auto found = const_cast<Assets::AssetManager*>( m_AssetManager )
                ->CreateAsset<Assets::PrefabAsset>( Assets::AssetPriority::High, m_SelectPathBuf );
            if ( found )
            {
                found->Load();
                prefab.Prefab = found->GetMetadata().Handle;
            }
        }

        ImGui::Separator();

        // ── Action buttons ────────────────────────────────────────────────
        if ( valid )
        {
            if ( ImGui::Button( ICON_MDI_REFRESH " Apply to Prefab" ) )
            {
                asset->CreateFromEntity( entity, *m_AssetManager );
                const std::string serialized = asset->Serialize();
                Common::Utils::FileSystem::WriteContentToFile( asset->GetMetadata().Filepath, serialized );
                LOG_INFO( "Prefab applied: {0}", asset->GetMetadata().Filepath.string() );
            }
            Utils::ImGuiUtilities::Tooltip( "Overwrite the prefab file with the current entity state" );

            ImGui::SameLine();

            if ( ImGui::Button( ICON_MDI_PACKAGE_VARIANT_CLOSED " Unpack" ) )
            {
                // Remove PrefabComponent from root — children keep their components
                entity.GetRegistry()->remove<ECS::PrefabComponent>( entity.GetHandle() );
                ImGui::PopStyleVar();
                Utils::ImGuiUtilities::PopID();
                return; // component is gone; stop rendering this widget
            }
            Utils::ImGuiUtilities::Tooltip( "Break the prefab link; entity and children become regular objects" );

            ImGui::SameLine();
        }

        if ( ImGui::Button( ICON_MDI_CLOSE " Clear" ) )
            prefab.Prefab = Common::UUID::Null();

        // ── Debug ─────────────────────────────────────────────────────────
        if ( ImGui::TreeNodeEx( "Debug", ImGuiTreeNodeFlags_Framed ) )
        {
            ImGui::Text( "Handle: %llu", (unsigned long long)prefab.Prefab );
            if ( asset )
                ImGui::Text( "Path: %s", asset->GetMetadata().Filepath.string().c_str() );
            ImGui::TreePop();
        }

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }
} // namespace Desert::Editor

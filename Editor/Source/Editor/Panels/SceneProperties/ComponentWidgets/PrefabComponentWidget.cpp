#include "PrefabComponentWidget.hpp"

#include <ImGui/imgui.h>
#include <Editor/Core/ImGuiUtilities.hpp>

#include <Engine/Assets/Prefab/PrefabAsset.hpp>

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

        // =========================
        // PREFAB HANDLE
        // =========================

        std::string handleStr = std::to_string( prefab.Prefab );

        Utils::ImGuiUtilities::Property( "Prefab", handleStr, Utils::ImGuiUtilities::PropertyFlag::ReadOnly );

        // =========================
        // STATUS
        // =========================

        bool valid = false;

        if ( m_AssetManager )
        {
            auto asset = m_AssetManager->FindByHandle<Assets::PrefabAsset>( prefab.Prefab );
            valid      = ( asset != nullptr );
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( "Status" );
        ImGui::NextColumn();

        if ( valid )
            ImGui::TextColored( ImVec4( 0, 1, 0, 1 ), "Valid" );
        else
            ImGui::TextColored( ImVec4( 1, 0, 0, 1 ), "Missing" );

        ImGui::NextColumn();

        ImGui::Columns( 1 );
        ImGui::Separator();

        // =========================
        // ACTIONS
        // =========================

        if ( ImGui::Button( "Select Prefab" ) )
        {
            // prefab.Prefab = selectedHandle;
        }

        ImGui::SameLine();

        if ( ImGui::Button( "Clear" ) )
        {
            prefab.Prefab = Assets::AssetHandle{};
        }

        // =========================
        // REAPPLY
        // =========================

        if ( valid && ImGui::Button( "Reapply Prefab" ) )
        {
             auto asset = m_AssetManager->FindByHandle<Assets::PrefabAsset>( prefab.Prefab );

             if ( scene && asset )
             {
                 // Store position to re-apply
                 std::optional<glm::vec3> position;
                 if ( entity.HasComponent<ECS::TransformComponent>() )
                     position = entity.GetComponent<ECS::TransformComponent>().Translation;

                 // We should destroy the current entity and instantiate the new one
                 scene->DestroyEntity( entity );

                 if ( position )
                     asset->Instantiate( scene, *m_AssetManager, &position.value() );
                 else
                     asset->Instantiate( scene, *m_AssetManager, nullptr );
             }
        }

        // =========================
        // DEBUG INFO
        // =========================

        if ( ImGui::TreeNodeEx( "Debug", ImGuiTreeNodeFlags_Framed ) )
        {
            ImGui::Text( "Handle: %llu", prefab.Prefab );

            ImGui::TreePop();
        }

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }
} // namespace Desert::Editor
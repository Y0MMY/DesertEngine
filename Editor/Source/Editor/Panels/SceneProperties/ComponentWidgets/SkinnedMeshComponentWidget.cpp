#include "SkinnedMeshComponentWidget.hpp"

#include <ImGui/imgui.h>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    SkinnedMeshComponentWidget::SkinnedMeshComponentWidget(
         const std::weak_ptr<Assets::AssetManager>& assetManager )
         : ComponentWidget( "Skinned Mesh" ), m_AssetManager( assetManager )
    {
    }

    void SkinnedMeshComponentWidget::Render( ECS::Entity& entity )
    {
        auto& skinnedMesh  = entity.GetComponent<ECS::SkinnedMeshComponent>();
        auto  assetManager = m_AssetManager.lock();
        if ( !assetManager )
            return;

        Utils::ImGuiUtilities::PushID();
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );

        auto meshAssets = assetManager->FindAllByType<Assets::MeshAsset>();

        std::string currentMeshName = "None";
        // if ( skinnedMesh.MeshHandle.IsValid() )
        {
            auto asset = assetManager->FindByHandle<Assets::MeshAsset>( skinnedMesh.MeshHandle );
            if ( asset )
            {
                currentMeshName = Common::Utils::FileSystem::GetFileName( asset->GetMetadata().Filepath );
            }
        }

        ImGui::Columns( 2 );
        ImGui::Separator();

        ImGui::TextUnformatted( "Mesh" );
        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );

        if ( ImGui::Button( currentMeshName.c_str(), ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
        {
            ImGui::OpenPopup( "skinned_mesh_selector" );
        }

        if ( ImGui::BeginPopup( "skinned_mesh_selector" ) )
        {
            static ImGuiTextFilter filter;
            filter.Draw( "##Search", 200 );
            ImGui::Separator();

            for ( const auto& [handle, asset] : meshAssets )
            {
                auto isSkinned = Runtime::ResourceRegistry::GetMeshService()->IsSkinned( handle );

                if ( !isSkinned.has_value() || !isSkinned.value() )
                    continue;

                const std::string name = Common::Utils::FileSystem::GetFileName( asset->GetMetadata().Filepath );

                if ( filter.PassFilter( name.c_str() ) )
                {
                    bool selected = skinnedMesh.MeshHandle == handle;
                    if ( ImGui::Selectable( name.c_str(), selected ) )
                    {
                        skinnedMesh.MeshHandle = handle;
                    }
                }
            }

            ImGui::EndPopup();
        }

        ImGui::PopItemWidth();
        ImGui::Columns( 1 );
        ImGui::Separator();

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }
} // namespace Desert::Editor

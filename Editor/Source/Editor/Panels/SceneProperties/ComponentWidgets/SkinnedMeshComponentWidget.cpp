#include "SkinnedMeshComponentWidget.hpp"

#include <ImGui/imgui.h>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>
#include <Editor/Core/ThemeManager.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include "Helper/MeshDetailsWidget.hpp"

#include <functional>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    SkinnedMeshComponentWidget::SkinnedMeshComponentWidget(
         const std::weak_ptr<Assets::AssetManager>& assetManager )
         : ComponentWidget( "Skinned Mesh" ), m_AssetManager( assetManager )
    {
    }

    void SkinnedMeshComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        auto& skinnedMesh  = entity.GetComponent<ECS::SkinnedMeshComponent>();
        auto  assetManager = m_AssetManager.lock();
        if ( !assetManager )
            return;

        Utils::ImGuiUtilities::PushID();
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );

        auto meshAssets = assetManager->FindAllByType<Assets::MeshAsset>();

        std::string currentMeshName = "None";
        auto        asset           = assetManager->FindByHandle<Assets::MeshAsset>( skinnedMesh.MeshHandle );
        if ( asset )
        {
            currentMeshName = Common::Utils::FileSystem::GetFileName( asset->GetMetadata().Filepath );
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
                {
                    continue;
                }

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

        MeshDetailsWidget::ShowMeshInfo( asset, skinnedMesh.MeshHandle );

        if ( !skinnedMesh.MeshHandle )
        {
            return;
        }

        auto meshService = Runtime::ResourceRegistry::GetMeshService();
        auto mesh        = meshService->Get( skinnedMesh.MeshHandle );

        if ( !mesh || !mesh->IsSkinned() )
        {
            return;
        }

        auto        skinned  = static_cast<SkinnedMesh*>( mesh );
        const auto& skeleton = skinned->GetSkeleton();

        if ( ImGui::CollapsingHeader( "Skeleton", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Indent(); // indent the sub-section content (collapsing headers don't auto-indent)

            const auto& bones = skeleton.GetBones();

            ImGui::Text( "Bone Count: %zu", bones.size() );
            ImGui::Separator();

            // Build child adjacency and collect root bones.
            std::unordered_map<size_t, std::vector<size_t>> children;
            std::vector<size_t>                             roots;
            for ( size_t i = 0; i < bones.size(); ++i )
            {
                if ( bones[i].ParentBoneID.has_value() )
                    children[bones[i].ParentBoneID.value()].push_back( i );
                else
                    roots.push_back( i );
            }

            std::function<void( size_t )> DrawBone;
            DrawBone = [&]( size_t boneIndex )
            {
                const auto& bone = bones[boneIndex];

                ImGuiTreeNodeFlags flags =
                     ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
                if ( children[boneIndex].empty() )
                    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;

                const std::string label =
                     std::string( ICON_MDI_BONE ) + "  " + bone.Name + "##" + std::to_string( boneIndex );
                if ( ImGui::TreeNodeEx( label.c_str(), flags ) )
                {
                    for ( auto child : children[boneIndex] )
                        DrawBone( child );
                    ImGui::TreePop();
                }
            };

            // UE-style: a single "Armature" root (accent-coloured) that holds the actual root bones.
            ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetIconColor() );
            const bool armatureOpen = ImGui::TreeNodeEx(
                 ICON_MDI_HUMAN "  Armature",
                 ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth );
            ImGui::PopStyleColor();
            if ( armatureOpen )
            {
                for ( size_t r : roots )
                    DrawBone( r );
                ImGui::TreePop();
            }

            ImGui::Unindent();
        }
    }

    DESERT_REGISTER_CUSTOM_COMPONENT(
         ECS::SkinnedMeshComponent, "Skinned Mesh", false,
         ( []( ECS::Entity& e, ::Desert::Core::Scene* s, const ComponentEditContext& ctx )
           { SkinnedMeshComponentWidget( ctx.AssetManager ).Render( e, s ); } ) )
} // namespace Desert::Editor

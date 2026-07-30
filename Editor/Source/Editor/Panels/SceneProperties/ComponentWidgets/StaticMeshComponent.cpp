#include "StaticMeshComponent.hpp"
#include <ImGui/imgui.h>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>
#include <Engine/Geometry/Mesh.hpp>

#include "MaterialsPanelComponent.hpp"

#include "Helper/MeshDetailsWidget.hpp"

#include "../../MeshEditor/MeshEditorPanel.hpp"

#include <Editor/Core/Rigging/RigBuilder.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>

#include <cfloat>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    StaticMeshComponentWidget::StaticMeshComponentWidget( const Assets::AssetManager* assetManager )
         : ComponentWidget( "3D Model" ), m_AssetManager( assetManager )
    {
    }

    void StaticMeshComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        auto& staticMesh = entity.GetComponent<ECS::StaticMeshComponent>();

        Utils::ImGuiUtilities::PushID();
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );

        ImGui::Columns( 2 );
        ImGui::Separator();

        ImGui::TextUnformatted( "Mesh Type" );
        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );

        const char* meshTypes[] = { "Asset", "Primitive" };
        int currentType = staticMesh.Primitive.has_value() ? 1 : 0;
        if ( ImGui::Combo( "##MeshType", &currentType, meshTypes, IM_ARRAYSIZE( meshTypes ) ) )
        {
            if ( currentType == 0 )
                staticMesh.Primitive.reset();
            else
                staticMesh.Primitive = Geometry::PrimitiveType::Cube;
        }

        ImGui::PopItemWidth();
        ImGui::NextColumn();

        if ( !staticMesh.Primitive.has_value() )
        {
            ImGui::TextUnformatted( "Asset" );
            ImGui::NextColumn();
            ImGui::PushItemWidth( -1 );

            std::string currentSelectionName = "Select Mesh";
            if ( staticMesh.MeshHandle )
            {
                auto meshAsset = m_AssetManager->FindByHandle<Assets::MeshAsset>( staticMesh.MeshHandle );
                if ( meshAsset )
                {
                    currentSelectionName = Common::Utils::FileSystem::GetFileName( meshAsset->GetMetadata().Filepath );
                }
            }

            if ( ImGui::Button( currentSelectionName.c_str(), ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
            {
                ImGui::OpenPopup( "mesh_selector" );
            }

            if ( ImGui::BeginPopup( "mesh_selector" ) )
            {
                auto meshAssets = m_AssetManager->FindAllByType<Assets::MeshAsset>();
                static ImGuiTextFilter meshFilter;
                meshFilter.Draw( "##Search", 200 );
                ImGui::Separator();

                for ( const auto& [handle, meshAsset] : meshAssets )
                {
                    const auto isSkinnedOpt = Runtime::ResourceRegistry::GetMeshService()->IsSkinned( handle );
                    if ( isSkinnedOpt.has_value() && isSkinnedOpt.value() )
                    {
                        continue;
                    }

                    const std::string& meshName = Common::Utils::FileSystem::GetFileName( meshAsset->GetMetadata().Filepath );
                    if ( meshFilter.PassFilter( meshName.c_str() ) )
                    {
                        if ( ImGui::Selectable( meshName.c_str(), staticMesh.MeshHandle == handle ) )
                        {
                            SetMeshAsset( staticMesh, handle );
                        }
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopItemWidth();
            ImGui::NextColumn();
        }
        else
        {
            ImGui::TextUnformatted( "Shape" );
            ImGui::NextColumn();
            ImGui::PushItemWidth( -1 );

            const char* shapes[] = { "Cube", "Sphere", "Pyramid", "Plane", "Cylinder", "Capsule" };
            int currentShape = (int)staticMesh.Primitive.value();
            if ( ImGui::Combo( "##Shape", &currentShape, shapes, IM_ARRAYSIZE( shapes ) ) )
            {
                staticMesh.Primitive = (Geometry::PrimitiveType)currentShape;
                // MeshECSSystem will handle the dynamic mesh generation/update
            }

            ImGui::PopItemWidth();
            ImGui::NextColumn();

            ImGui::TextUnformatted( "Geometry Editor" );
            ImGui::NextColumn();
            if ( ImGui::Button( "Open Mesh Editor", ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
            {
                if ( auto* meshEditor = MeshEditorPanel::GetInstance() )
                {
                    meshEditor->SetTarget( entity );
                }
            }
            ImGui::NextColumn();
        }

        ImGui::Columns( 1 );
        ImGui::Separator();

        {
            static MaterialComponentWidget materialComponent( m_AssetManager );
            materialComponent.Render( entity );
        }

        RenderRigging( entity, staticMesh );

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }

    void StaticMeshComponentWidget::RenderRigging( ECS::Entity& entity, ECS::StaticMeshComponent& staticMesh )
    {
        // Rigging needs asset-backed CPU geometry (primitives/procedural have no StaticMeshAsset vertices).
        if ( staticMesh.Primitive.has_value() || !staticMesh.MeshHandle )
            return;
        if ( !entity.HasComponent<ECS::UUIDComponent>() )
            return;

        const Common::UUID uuid = entity.GetComponent<ECS::UUIDComponent>().UUID;

        // Seed / default placement = the mesh's local AABB centre.
        glm::vec3 center( 0.0f );
        if ( auto asset = m_AssetManager->FindByHandle<Assets::StaticMeshAsset>( staticMesh.MeshHandle ) )
        {
            const auto& verts = asset->GetVertices();
            if ( !verts.empty() )
            {
                glm::vec3 mn( FLT_MAX ), mx( -FLT_MAX );
                for ( const auto& v : verts )
                {
                    mn = glm::min( mn, v.Position );
                    mx = glm::max( mx, v.Position );
                }
                center = 0.5f * ( mn + mx );
            }
        }

        ImGui::Separator();
        if ( !ImGui::CollapsingHeader( "Rigging (Skeleton)" ) )
            return;

        const bool   riggingThis = RigBuilder::IsActive() && RigBuilder::Target() == uuid;
        const ImVec2 full( ImGui::GetContentRegionAvail().x, 0 );

        if ( !riggingThis )
        {
            if ( RigBuilder::IsActive() )
                ImGui::TextDisabled( "Another mesh is being rigged." );
            ImGui::TextWrapped( "Place bones on this static mesh, then convert it to a skinned mesh with "
                                "automatic vertex weights. Pose the bones afterwards in Skeleton Edit mode." );
            if ( ImGui::Button( "Add Skeleton / Rig this Mesh", full ) )
                RigBuilder::Begin( uuid, center );
            return;
        }

        const auto& bones = RigBuilder::Bones();
        const int   sel   = RigBuilder::SelectedBone();

        ImGui::TextDisabled( "Bones (%d)", static_cast<int>( bones.size() ) );
        for ( int i = 0; i < static_cast<int>( bones.size() ); ++i )
        {
            ImGui::PushID( i );
            std::string label = bones[i].Name;
            if ( bones[i].Parent < 0 )
                label += "  (root)";
            if ( ImGui::Selectable( label.c_str(), i == sel ) )
                RigBuilder::SelectBone( i );
            ImGui::PopID();
        }

        ImGui::Separator();
        if ( sel >= 0 && sel < static_cast<int>( bones.size() ) )
        {
            glm::vec3 head = bones[sel].Head;
            if ( ImGui::DragFloat3( "Head", &head.x, 0.01f ) )
                RigBuilder::SetHead( sel, head );
        }

        if ( ImGui::Button( "Add Child Bone" ) )
        {
            const glm::vec3 head = ( sel >= 0 ) ? bones[sel].Head + glm::vec3( 0.0f, 0.5f, 0.0f ) : center;
            RigBuilder::AddBone( sel, head );
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Delete Bone" ) )
            RigBuilder::DeleteBone( sel );

        ImGui::Separator();
        if ( ImGui::Button( "Convert to Skinned", full ) )
            RigBuilder::RequestConvert();
        if ( ImGui::Button( "Cancel", full ) )
            RigBuilder::Cancel();
    }

    void StaticMeshComponentWidget::SetMeshAsset( ECS::StaticMeshComponent& staticMesh, const Assets::AssetHandle& handle )
    {
        staticMesh.MeshHandle = handle;
        staticMesh.Primitive.reset();
        // Load default materials from asset...
    }

    std::string StaticMeshComponentWidget::GetPrimitiveName( const ECS::StaticMeshComponent& staticMesh ) const
    {
        if ( !staticMesh.Primitive ) return "None";
        switch ( *staticMesh.Primitive )
        {
            case Geometry::PrimitiveType::Cube: return "Cube";
            case Geometry::PrimitiveType::Sphere: return "Sphere";
            case Geometry::PrimitiveType::Plane: return "Plane";
            default: return "Primitive";
        }
    }

    DESERT_REGISTER_CUSTOM_COMPONENT(
         ECS::StaticMeshComponent, "3D Model", false,
         ( []( ECS::Entity& e, ::Desert::Core::Scene* s, const ComponentEditContext& ctx )
           { StaticMeshComponentWidget( ctx.AssetMgr() ).Render( e, s ); } ) )

} // namespace Desert::Editor

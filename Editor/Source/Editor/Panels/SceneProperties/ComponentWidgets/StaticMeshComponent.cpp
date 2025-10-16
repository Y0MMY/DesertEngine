#include "StaticMeshComponent.hpp"
#include <ImGui/imgui.h>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Engine/Geometry/Mesh.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    StaticMeshComponentWidget::StaticMeshComponentWidget( const std::weak_ptr<Assets::AssetManager>& assetManager )
         : ComponentWidget( "3D Model" ), m_AssetManager( assetManager )
    {
    }

    void StaticMeshComponentWidget::Render( ECS::Entity& entity )
    {
        auto& staticMesh = entity.GetComponent<ECS::StaticMeshComponent>();

        Utils::ImGuiUtilities::PushID();
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );

        // Render appropriate section based on type
        switch ( staticMesh.GetMeshType() )
        {
            case ECS::StaticMeshComponent::Type::Asset:
                RenderAssetSection( staticMesh );
                break;

            case ECS::StaticMeshComponent::Type::Primitive:
                RenderPrimitiveSection( staticMesh );
                break;

            case ECS::StaticMeshComponent::Type::None:
            default:
                ImGui::TextDisabled( "No mesh selected" );
                break;
        }

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }

    void StaticMeshComponentWidget::RenderAssetSection( ECS::StaticMeshComponent& staticMesh )
    {
        const auto assetManager = m_AssetManager.lock();
        if ( !assetManager )
            return;

        auto meshAssets = assetManager->FindAllByType<Assets::MeshAsset>();

        const auto& currentSelectedMesh =
             staticMesh.MeshHandle.has_value()
                  ? assetManager->FindByHandle<Assets::MeshAsset>( *staticMesh.MeshHandle )
                  : nullptr;

        std::string currentMeshName =
             currentSelectedMesh
                  ? Common::Utils::FileSystem::GetFileName( currentSelectedMesh->GetMetadata().Filepath )
                  : "None";

        ImGui::Columns( 2 );
        ImGui::Separator();

        // Mesh selection section
        ImGui::TextUnformatted( "Mesh" );
        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );

        if ( ImGui::Button( currentMeshName.c_str(), ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
        {
            ImGui::OpenPopup( "mesh_selector" );
        }

        if ( ImGui::BeginPopup( "mesh_selector" ) )
        {
            static ImGuiTextFilter meshFilter;
            meshFilter.Draw( "##Search", 200 );
            ImGui::Separator();

            for ( const auto& [handle, meshAsset] : meshAssets )
            {
                const std::string& meshName =
                     Common::Utils::FileSystem::GetFileName( meshAsset->GetMetadata().Filepath );

                if ( meshFilter.PassFilter( meshName.c_str() ) )
                {
                    bool isSelected =
                         ( staticMesh.MeshHandle.has_value() && ( *staticMesh.MeshHandle ) == handle );
                    if ( ImGui::Selectable( meshName.c_str(), isSelected ) )
                    {
                        staticMesh.MeshHandle = handle;
                    }

                    if ( isSelected )
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }

            if ( meshAssets.empty() )
            {
                ImGui::TextDisabled( "No mesh assets available" );
            }

            ImGui::EndPopup();
        }

        ImGui::PopItemWidth();
        ImGui::NextColumn();
        ImGui::Columns( 1 );
        ImGui::Separator();

        // Show mesh info if mesh is loaded
        if ( currentSelectedMesh )
        {
            if ( ImGui::TreeNodeEx( "Mesh Details", ImGuiTreeNodeFlags_Framed ) )
            {
                ImGui::Columns( 2 );

                // File path
                ImGui::TextUnformatted( "File Path" );
                ImGui::NextColumn();
                ImGui::TextUnformatted( currentSelectedMesh->GetMetadata().Filepath.string().c_str() );
                Utils::ImGuiUtilities::Tooltip( currentSelectedMesh->GetMetadata().Filepath.string().c_str() );
                ImGui::NextColumn();

                ImGui::Columns( 1 );

                if ( ImGui::TreeNodeEx( "Meshes", ImGuiTreeNodeFlags_Framed ) )
                {
                    int MeshIndex = 0;

                    const auto& submeshes = Runtime::ResourceRegistry::GetMeshService()
                                                 ->Get( *staticMesh.MeshHandle )
                                                 ->GetSubmeshes();
                    for ( auto mesh : submeshes )
                    {
                        const std::string meshName = std::format( "Submesh {}", MeshIndex );

                        if ( ImGui::TreeNodeEx( (const char*)meshName.c_str(), ImGuiTreeNodeFlags_Framed ) )
                        {
                            ImGui::Indent();
                            ImGui::Columns( 2 );
                            auto triangles = mesh.VertexCount / 3;
                            Utils::ImGuiUtilities::Property( "Triangle Count", triangles,
                                                             Utils::ImGuiUtilities::PropertyFlag::ReadOnly );
                            Utils::ImGuiUtilities::Property( "Vertex Count", mesh.VertexCount,
                                                             Utils::ImGuiUtilities::PropertyFlag::ReadOnly );
                            Utils::ImGuiUtilities::Property( "Index Count", mesh.IndexCount,
                                                             Utils::ImGuiUtilities::PropertyFlag::ReadOnly );

                            ImGui::Columns( 1 );

                            ImGui::Unindent();
                            ImGui::TreePop();
                        }

                        MeshIndex++;
                    }
                    ImGui::TreePop();
                }
                ImGui::TreePop();

            }
        }
    }

    void StaticMeshComponentWidget::RenderPrimitiveSection( ECS::StaticMeshComponent& staticMesh )
    {
        if ( !staticMesh.PrimitiveShape.has_value() )
        {
            return;
        }

        ImGui::Columns( 2 );
        ImGui::Separator();

        // Primitive selection section
        ImGui::TextUnformatted( "Primitive" );
        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );

        // Get current primitive name
        std::string currentPrimitiveName = PrimitiveMeshFactory::GetPrimitiveName( *staticMesh.PrimitiveShape );

        ImGui::TextUnformatted( currentPrimitiveName.c_str() );

        ImGui::PopItemWidth();
        ImGui::NextColumn();
        ImGui::Columns( 1 );
        ImGui::Separator();

        // Show primitive info
        if ( ImGui::TreeNodeEx( "Primitive Details", ImGuiTreeNodeFlags_Framed ) )
        {
            ImGui::Columns( 2 );

            ImGui::TextUnformatted( "Type" );
            ImGui::NextColumn();
            ImGui::TextUnformatted( currentPrimitiveName.c_str() );
            ImGui::NextColumn();

            ImGui::Columns( 1 );
            ImGui::TreePop();
        }
    }

} // namespace Desert::Editor
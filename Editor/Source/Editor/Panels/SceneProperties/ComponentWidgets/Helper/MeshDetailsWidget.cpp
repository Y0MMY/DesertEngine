#include "MeshDetailsWidget.hpp"

#include <Editor/Core/ImGuiUtilities.hpp>
#include <ImGui/imgui.h>

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    void MeshDetailsWidget::ShowMeshInfo( const Assets::Asset<Assets::MeshAsset>& currentSelectedMesh,
                                          const Assets::AssetHandle&              meshHandle )
    {
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

                    const auto& submeshes =
                         Runtime::ResourceRegistry::GetMeshService()->Get( meshHandle )->GetSubmeshes();
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

} // namespace Desert::Editor
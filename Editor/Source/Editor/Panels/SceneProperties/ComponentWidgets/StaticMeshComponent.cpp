#include "StaticMeshComponent.hpp"
#include <ImGui/imgui.h>

#include <Editor/Core/ImGuiUtilities.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    StaticMeshComponentWidget::StaticMeshComponentWidget( const std::weak_ptr<Assets::AssetManager>& assetManager )
         : ComponentWidget( "3D Model" ), m_AssetManager( assetManager )
    {
    }

    void StaticMeshComponentWidget::Render( ECS::Entity& entity )
    {
        const auto assetManager = m_AssetManager.lock();
        auto       meshAssets   = assetManager->FindAllByType<Assets::MeshAsset>();

        auto&       staticMesh          = entity.GetComponent<ECS::StaticMeshComponent>();
        const auto& currentSelectedMesh = assetManager->FindByHandle<Assets::MeshAsset>( staticMesh.MeshHandle );

        std::string currentMeshName =
             currentSelectedMesh
                  ? Common::Utils::FileSystem::GetFileName( currentSelectedMesh->GetMetadata().Filepath )
                  : "None";

        Utils::ImGuiUtilities::PushID();
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );

        // Mesh selection section
        ImGui::Columns( 2 );
        ImGui::Separator();

        ImGui::TextUnformatted( "Mesh" );
        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );

        if ( ImGui::BeginCombo( "##MeshSelector", currentMeshName.c_str(), 0 ) )
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
                    bool isSelected = ( staticMesh.MeshHandle == handle );
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

            ImGui::EndCombo();
        }

        ImGui::PopItemWidth();
        ImGui::NextColumn();
        ImGui::Columns( 1 ); // Важно: сбросить колонки
        ImGui::Separator();
        ImGui::PopStyleVar();

        // Show mesh info if mesh is loaded
        if ( currentSelectedMesh )
        {
            // Убедитесь, что мы не в режиме колонок перед отрисовкой деталей
            ImGui::Columns( 1 ); // Дополнительный сброс для уверенности

            if ( ImGui::TreeNodeEx( "Mesh Details", ImGuiTreeNodeFlags_Framed ) )
            {
                ImGui::Columns( 2 );

                // File path
                ImGui::TextUnformatted( "File Path" );
                ImGui::NextColumn();
                ImGui::TextUnformatted( "currentSelectedMesh->GetMetadata().Filepath.c_str() ");
                Utils::ImGuiUtilities::Tooltip( "currentSelectedMesh->GetMetadata().Filepath.c_str() ");
                ImGui::NextColumn();

                ImGui::Columns( 1 );
                ImGui::TreePop();
            }
        }

        Utils::ImGuiUtilities::PopID();
    }

} // namespace Desert::Editor
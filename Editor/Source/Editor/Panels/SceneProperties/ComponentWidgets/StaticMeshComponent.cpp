#include "StaticMeshComponent.hpp"
#include <ImGui/imgui.h>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Engine/Geometry/Mesh.hpp>

#include "MaterialsPanelComponent.hpp"

#include "Helper/MeshDetailsWidget.hpp"

#include <Editor/Core/PrimitiveMeshFactory.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    StaticMeshComponentWidget::StaticMeshComponentWidget( const Assets::AssetManager* assetManager )
         : ComponentWidget( "3D Model" ), m_AssetManager( assetManager )
    {
    }

    void StaticMeshComponentWidget::Render( ECS::Entity& entity )
    {
        auto& staticMesh = entity.GetComponent<ECS::StaticMeshComponent>();

        Utils::ImGuiUtilities::PushID();
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );

        ImGui::Columns( 2 );
        ImGui::Separator();

        ImGui::TextUnformatted( "Mesh" );
        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );

        std::string currentSelectionName = "Select Mesh";

        if ( staticMesh.MeshHandle )
        {
            const bool isAsset =
                 m_AssetManager->FindByHandle<Assets::MeshAsset>( staticMesh.MeshHandle ) != nullptr;
            if ( isAsset )
            {
                auto meshAsset = m_AssetManager->FindByHandle<Assets::MeshAsset>( staticMesh.MeshHandle );
                if ( meshAsset )
                {
                    currentSelectionName =
                         Common::Utils::FileSystem::GetFileName( meshAsset->GetMetadata().Filepath );
                }
            }
            else
            {
                currentSelectionName = GetPrimitiveName( staticMesh );
            }
        }

        if ( ImGui::Button( currentSelectionName.c_str(), ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
        {
            ImGui::OpenPopup( "mesh_selector" );
        }

        ImGui::PopItemWidth();
        ImGui::NextColumn();
        ImGui::Columns( 1 );
        ImGui::Separator();

        if ( ImGui::BeginPopup( "mesh_selector" ) )
        {
            auto                   meshAssets = m_AssetManager->FindAllByType<Assets::MeshAsset>();
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

                const std::string& meshName =
                     Common::Utils::FileSystem::GetFileName( meshAsset->GetMetadata().Filepath );

                if ( meshFilter.PassFilter( meshName.c_str() ) )
                {
                    bool isSelected = ( staticMesh.MeshHandle == handle );

                    if ( ImGui::Selectable( meshName.c_str(), isSelected ) )
                    {
                        SetMeshAsset( staticMesh, handle );
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

        if ( staticMesh.MeshHandle )
        {
            const bool isAsset =
                 m_AssetManager->FindByHandle<Assets::MeshAsset>( staticMesh.MeshHandle ) != nullptr;
            if ( isAsset )
            {
                auto meshAsset = m_AssetManager->FindByHandle<Assets::MeshAsset>( staticMesh.MeshHandle );
                MeshDetailsWidget::ShowMeshInfo( meshAsset, staticMesh.MeshHandle );
            }
            else
            {
                RenderPrimitiveInfo( staticMesh );
            }
        }
        else
        {
            ImGui::TextDisabled( "No mesh or primitive selected. Click the button above to select." );
        }

        {
            static MaterialComponentWidget materialComponent( m_AssetManager );
            materialComponent.Render( entity );
        }

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }

    void StaticMeshComponentWidget::RenderPrimitiveInfo( ECS::StaticMeshComponent& staticMesh )
    {
        if ( ImGui::TreeNodeEx( "Primitive Details", ImGuiTreeNodeFlags_Framed ) )
        {
            ImGui::Columns( 2 );

            ImGui::TextUnformatted( "Type" );
            ImGui::NextColumn();
            ImGui::TextUnformatted( GetPrimitiveName( staticMesh ).c_str() );
            ImGui::NextColumn();

            ImGui::NextColumn();
            ImGui::Columns( 1 );
            ImGui::TreePop();
        }
    }

    void StaticMeshComponentWidget::SetMeshAsset( ECS::StaticMeshComponent&  staticMesh,
                                                  const Assets::AssetHandle& handle )
    {
        staticMesh.MeshHandle        = handle;
        const auto selectedMeshAsset = m_AssetManager->FindByHandle<Assets::MeshAsset>( handle );
        if ( selectedMeshAsset )
        {
            const auto handles = selectedMeshAsset->GetMaterialHandles();
            staticMesh.MaterialSlots.reserve( handles.size() );
            for ( const auto materialHandle : handles )
            {
                staticMesh.MaterialSlots.emplace_back(
                     Runtime::ResourceRegistry::GetMaterialService()->GetAssetHandleByExternal( materialHandle ) );
            }
        }

        // staticMesh.PrimitiveShape.reset();
    }

    void StaticMeshComponentWidget::SetPrimitive( ECS::StaticMeshComponent& staticMesh, PrimitiveType type )
    {
        /* auto primitiveMesh        = PrimitiveMeshFactory::CreatePrimitive( type );
         staticMesh.MeshHandle     = primitiveMesh->GetHandle();
         staticMesh.PrimitiveShape = std::make_shared<PrimitiveShape>( type );*/
    }

    bool StaticMeshComponentWidget::IsPrimitiveSelected( const ECS::StaticMeshComponent& staticMesh,
                                                         PrimitiveType                   type ) const
    {
        return false; // staticMesh.PrimitiveShape && staticMesh.PrimitiveShape->GetType() == type;
    }

    std::string StaticMeshComponentWidget::GetPrimitiveName( const ECS::StaticMeshComponent& staticMesh ) const
    {
        /* if ( staticMesh.PrimitiveShape )
         {
             switch ( staticMesh.PrimitiveShape->GetType() )
             {
                 case PrimitiveType::Cube:
                     return "Cube";
                 case PrimitiveType::Sphere:
                     return "Sphere";
                 case PrimitiveType::Cylinder:
                     return "Cylinder";
                 case PrimitiveType::Plane:
                     return "Plane";
                 case PrimitiveType::Cone:
                     return "Cone";
                 default:
                     return "Unknown Primitive";
             }
         }*/
        return "Primitive";
    }

    void StaticMeshComponentWidget::RenderAssetSection( ECS::StaticMeshComponent& staticMesh )
    {

        auto meshAssets = m_AssetManager->FindAllByType<Assets::MeshAsset>();

        const auto& currentSelectedMesh = m_AssetManager->FindByHandle<Assets::MeshAsset>( staticMesh.MeshHandle );

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
                const auto isSkinnedOpt = Runtime::ResourceRegistry::GetMeshService()->IsSkinned( handle );
                if ( isSkinnedOpt.has_value() && isSkinnedOpt.value() )
                {
                    continue;
                }
                const std::string& meshName =
                     Common::Utils::FileSystem::GetFileName( meshAsset->GetMetadata().Filepath );

                if ( meshFilter.PassFilter( meshName.c_str() ) )
                {
                    bool isSelected = ( ( staticMesh.MeshHandle ) == handle );
                    if ( ImGui::Selectable( meshName.c_str(), isSelected ) )
                    {
                        const auto selectedMeshAsset = m_AssetManager->FindByHandle<Assets::MeshAsset>( handle );
                        staticMesh.MeshHandle        = handle;
                        const auto handles           = selectedMeshAsset->GetMaterialHandles();
                        staticMesh.MaterialSlots.reserve( handles.size() );
                        for ( const auto handle : handles )
                        {
                            staticMesh.MaterialSlots.emplace_back(
                                 Runtime::ResourceRegistry::GetMaterialService()->GetAssetHandleByExternal(
                                      handle ) );
                        }
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
        MeshDetailsWidget::ShowMeshInfo( currentSelectedMesh, staticMesh.MeshHandle );
    }

    void StaticMeshComponentWidget::RenderPrimitiveSection( ECS::StaticMeshComponent& staticMesh )
    {

        ImGui::Columns( 2 );
        ImGui::Separator();

        // Primitive selection section
        ImGui::TextUnformatted( "Primitive" );
        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );

        // Get current primitive name
        std::string currentPrimitiveName =
             "Cube"; // PrimitiveMeshFactory::GetPrimitiveName( *staticMesh.PrimitiveShape );

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
#include "MaterialsPanelComponent.hpp"
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <ImGui/imgui.h>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Constants.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>

#include <Editor/Panels/PropertyEditor/PropertyEditorBuilder.hpp>
#include <Engine/Assets/Mesh/PBRMaterialAsset.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/Material/MaterialService.hpp>

#include <glm/gtc/type_ptr.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    MaterialComponentWidget::MaterialComponentWidget( const Assets::AssetManager* assetManager )
         : m_AssetManager( assetManager )
    {
        m_UIHelper = std::make_unique<Editor::UI::UIHelper>();
        m_UIHelper->Init();
    }

    void MaterialComponentWidget::Render( ECS::Entity& entity )
    {
        auto& materialComp = entity.GetComponent<ECS::StaticMeshComponent>();
        RenderMaterialProperties( materialComp );
    }

    size_t MaterialComponentWidget::GetSubmeshCount( const ECS::StaticMeshComponent& meshComp ) const
    {
        if ( meshComp.MeshHandle )
        {
            if ( auto* meshAsset = Runtime::ResourceRegistry::GetMeshService()->GetAsset( meshComp.MeshHandle ) )
            {
                const size_t count = meshAsset->GetMaterialHandles().size();
                if ( count > 0 )
                    return count;
            }
        }
        return 1; // single implicit slot when the mesh exposes none (e.g. primitives)
    }

    Assets::AssetHandle MaterialComponentWidget::CreateAndRegisterMaterial()
    {
        if ( !m_AssetManager )
            return {};

        // Unique on-disk name so each created slot is an independent, persisted asset.
        const std::string name = "Material_" + std::to_string( static_cast<uint64_t>( Common::UUID() ) ) +
                                 Common::Constants::Extensions::MATERIAL_EXTENSION;
        const Common::Filepath path = Common::Constants::Path::MATERIAL_PATH / name;

        auto asset = const_cast<Assets::AssetManager&>( *m_AssetManager )
                          .CreateAsset<Assets::PBRMaterialAsset>( Assets::AssetPriority::High, path );
        if ( !asset )
            return {};

        Common::Utils::FileSystem::WriteContentToFile( asset->GetMetadata().Filepath, asset->Save() );
        Runtime::ResourceRegistry::GetMaterialService()->Register( asset );
        return asset->GetMetadata().Handle;
    }

    void MaterialComponentWidget::AssignMaterialFromPath( ECS::StaticMeshComponent& meshComp, size_t slot,
                                                          const std::string& assetPath )
    {
        if ( !m_AssetManager )
            return;

        auto asset = m_AssetManager->FindByPath<Assets::PBRMaterialAsset>( assetPath );
        if ( !asset )
            asset = const_cast<Assets::AssetManager&>( *m_AssetManager )
                         .CreateAsset<Assets::PBRMaterialAsset>( Assets::AssetPriority::High, assetPath );
        if ( !asset )
            return;

        const auto handle = asset->GetMetadata().Handle;
        if ( !Runtime::ResourceRegistry::GetMaterialService()->Get( handle ) )
            Runtime::ResourceRegistry::GetMaterialService()->Register( asset );

        if ( slot < meshComp.MaterialSlots.size() )
            meshComp.MaterialSlots[slot] = handle;
        else
            meshComp.MaterialSlots.push_back( handle );

        // Force MeshECSSystem to rebuild the runtime instances (it only rebuilds on slot-count change,
        // so an in-place handle swap needs an explicit reset).
        meshComp.RuntimeMaterialInstances.clear();
    }

    void MaterialComponentWidget::RenderMaterialProperties( ECS::StaticMeshComponent& meshComp )
    {
        Utils::ImGuiUtilities::PushID();
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );

        if ( ImGui::TreeNodeEx( "Materials", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            const size_t submeshCount = GetSubmeshCount( meshComp );

            if ( meshComp.MaterialSlots.empty() )
            {
                ImGui::TextDisabled( "No material slots" );
            }

            // Create-material affordance: fill missing slots up to the mesh's submesh count with fresh,
            // editable material assets. Also a drop target for dragging a .demat from the File Explorer.
            if ( meshComp.MaterialSlots.size() < submeshCount )
            {
                const std::string addLabel =
                     "Add Material (" + std::to_string( submeshCount - meshComp.MaterialSlots.size() ) + ")";
                if ( ImGui::Button( addLabel.c_str(), ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
                {
                    while ( meshComp.MaterialSlots.size() < submeshCount )
                        meshComp.MaterialSlots.push_back( CreateAndRegisterMaterial() );
                    meshComp.RuntimeMaterialInstances.clear();
                }
                if ( ImGui::BeginDragDropTarget() )
                {
                    if ( const ImGuiPayload* p = ImGui::AcceptDragDropPayload( "MATERIAL_ASSET" ) )
                    {
                        const std::string path( static_cast<const char*>( p->Data ),
                                                p->DataSize > 0 ? p->DataSize - 1 : 0 );
                        AssignMaterialFromPath( meshComp, meshComp.MaterialSlots.size(), path );
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            for ( size_t i = 0; i < meshComp.MaterialSlots.size(); ++i )
            {
                ImGui::PushID( static_cast<int>( i ) );

                const auto  handle = meshComp.MaterialSlots[i];
                const auto  asset  = m_AssetManager
                                         ? m_AssetManager->FindByHandle<Assets::PBRMaterialAsset>( handle )
                                         : nullptr;

                const std::string title = "Element " + std::to_string( i );
                const bool        nodeOpen = ImGui::TreeNodeEx(
                    title.c_str(), ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen );

                // Drop an existing material asset onto this slot to assign it.
                if ( ImGui::BeginDragDropTarget() )
                {
                    if ( const ImGuiPayload* p = ImGui::AcceptDragDropPayload( "MATERIAL_ASSET" ) )
                    {
                        const std::string path( static_cast<const char*>( p->Data ),
                                                p->DataSize > 0 ? p->DataSize - 1 : 0 );
                        AssignMaterialFromPath( meshComp, i, path );
                    }
                    ImGui::EndDragDropTarget();
                }

                if ( nodeOpen )
                {
                    if ( asset )
                    {
                        // Fully reflection-driven: the entire property UI is built from PBRMaterialData's
                        // PROPERTY() metadata — no per-parameter editor code here.
                        const bool changed =
                             PropertyEditorBuilder::Draw( &asset->Data(), "PBRMaterialData", m_AssetManager );

                        // Live edit -> viewport: push the edited asset data into the runtime material
                        // (one StaticMaterialPBR per asset handle, shared by all meshes using it).
                        if ( changed )
                        {
                            if ( auto* runtime = Runtime::ResourceRegistry::GetMaterialService()->Get( handle ) )
                                Graphic::MaterialFactory::ApplyPBRAsset(
                                     *static_cast<Graphic::StaticMaterialPBR*>( runtime ), *asset );
                        }

                        if ( ImGui::Button( "Save", ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
                        {
                            Common::Utils::FileSystem::WriteContentToFile( asset->GetMetadata().Filepath,
                                                                           asset->Save() );
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled( "Unassigned material slot" );
                    }
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            ImGui::TreePop();
        }

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }

} // namespace Desert::Editor

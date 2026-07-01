#include "MaterialsPanelComponent.hpp"
#include <Editor/Core/DragPayloads.hpp>
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <ImGui/imgui.h>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Constants.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>

#include <Editor/Panels/PropertyEditor/PropertyEditorBuilder.hpp>
#include <Editor/Widgets/ThumbnailCache.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Engine/Assets/Mesh/PBRMaterialAsset.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/Material/MaterialService.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <filesystem>
#include <system_error>

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

        // Meaningful, human-readable filename (NO handle in the name): "Material", then "Material_1", ... —
        // first free index in the material folder. The stable identity lives INSIDE the file (MaterialId).
        const std::string         ext = Common::Constants::Extensions::MATERIAL_EXTENSION;
        const std::filesystem::path dir = Common::Constants::Path::MATERIAL_PATH;
        std::error_code             ec;
        std::filesystem::create_directories( dir, ec );

        std::filesystem::path path = dir / ( "Material" + ext );
        for ( int n = 1; std::filesystem::exists( path, ec ); ++n )
            path = dir / ( "Material_" + std::to_string( n ) + ext );

        // loadAfterCreate = false: the file doesn't exist yet — creating with auto-load would try to read
        // a missing file and abort. The reflected data defaults are valid in memory; Save() writes them.
        auto asset = const_cast<Assets::AssetManager&>( *m_AssetManager )
                          .CreateAsset<Assets::PBRMaterialAsset>( Assets::AssetPriority::High,
                                                                  path.generic_string(), false );
        if ( !asset )
            return {};

        // Stamp a stable id so this material can be referenced externally (e.g. baked into a mesh later).
        asset->Data().MaterialId = Common::UUID();

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

        const bool materialsOpen =
             ImGui::TreeNodeEx( "Materials", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen );

        // Drop a .mat onto the Materials header (works open or collapsed): create slots up to the submesh
        // count if there are none, then assign the dropped material to EVERY slot.
        if ( ImGui::BeginDragDropTarget() )
        {
            if ( const ImGuiPayload* p = ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::MaterialAsset ) )
            {
                const std::string path( static_cast<const char*>( p->Data ),
                                        p->DataSize > 0 ? p->DataSize - 1 : 0 );
                const size_t      count = GetSubmeshCount( meshComp );
                while ( meshComp.MaterialSlots.size() < count )
                    meshComp.MaterialSlots.push_back( Common::UUID::Null() );
                for ( size_t s = 0; s < meshComp.MaterialSlots.size(); ++s )
                    AssignMaterialFromPath( meshComp, s, path );
            }
            ImGui::EndDragDropTarget();
        }

        if ( materialsOpen )
        {
            const size_t submeshCount = GetSubmeshCount( meshComp );

            // --- Per-submesh visibility (its OWN section, separate from material slots) ---
            // Toggles bit i of HiddenSubmeshes -> the renderer skips that submesh (independent of the
            // whole-entity VisibilityComponent). Lives here in plain rows (NOT on the framed "Element" tree
            // header) because a framed TreeNodeEx swallows the row click, so the eye SmallButton never fired
            // (it only collapsed the node). Only shown when there's more than one submesh to toggle.
            if ( submeshCount > 1 )
            {
                if ( ImGui::TreeNodeEx( "Submesh Visibility", ImGuiTreeNodeFlags_DefaultOpen ) )
                {
                    for ( size_t s = 0; s < submeshCount && s < 64; ++s )
                    {
                        ImGui::PushID( static_cast<int>( 1000 + s ) );
                        const bool hidden = ( meshComp.HiddenSubmeshes >> s ) & 1ull;
                        if ( ImGui::SmallButton( hidden ? ICON_MDI_EYE_OFF : ICON_MDI_EYE ) )
                            meshComp.HiddenSubmeshes ^= ( 1ull << s );
                        ImGui::SameLine();
                        ImGui::Text( "Submesh %zu%s", s, hidden ? "  (hidden)" : "" );
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }

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
                    if ( const ImGuiPayload* p = ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::MaterialAsset ) )
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
                    if ( const ImGuiPayload* p = ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::MaterialAsset ) )
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
                        bool changed = PropertyEditorBuilder::Draw( &asset->Data(), "PBRMaterialData",
                                                                    m_AssetManager, m_UIHelper.get() );

                        // UV Tiling — manual widget (the field is std::optional, so the reflected property UI
                        // doesn't draw it). Multiplies the surface UVs before sampling albedo/normal/opacity,
                        // so the texture repeats N times (e.g. a tiling brick/grass surface on a wall/terrain).
                        {
                            glm::vec2 tiling = asset->Data().UVTiling.value_or( glm::vec2( 1.0f ) );
                            if ( ImGui::DragFloat2( "UV Tiling", &tiling.x, 0.05f, 0.01f, 256.0f ) )
                            {
                                asset->Data().UVTiling = tiling;
                                changed                = true;
                            }
                        }

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
                            // Drop ONLY this material's cached thumbnail so the asset browser re-renders it
                            // with the new look immediately (no waiting on the modtime check; others untouched).
                            std::error_code ec;
                            std::filesystem::remove(
                                 ThumbnailCache::DiskPath( asset->GetMetadata().Filepath.generic_string() ), ec );
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled( "Unassigned material slot" );
                        // Pre-existing-but-empty slot (e.g. a mesh with no embedded material): let the user
                        // create a fresh editable material right here (the "Add Material" button above only
                        // shows when there are FEWER slots than submeshes). Also accepts a dropped .demat.
                        if ( ImGui::Button( "Create Material",
                                            ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
                        {
                            if ( const auto h = CreateAndRegisterMaterial() )
                            {
                                meshComp.MaterialSlots[i] = h;
                                meshComp.RuntimeMaterialInstances.clear();
                            }
                        }
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

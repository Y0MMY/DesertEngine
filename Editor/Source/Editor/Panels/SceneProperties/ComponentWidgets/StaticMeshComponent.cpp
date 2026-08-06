#include "StaticMeshComponent.hpp"
#include <ImGui/imgui.h>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>
#include <Engine/Geometry/Mesh.hpp>

#include "MaterialsPanelComponent.hpp"

#include "Helper/MeshDetailsWidget.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/ThemeManager.hpp>
#include <Editor/Core/MeshResolve.hpp>
#include <Editor/Widgets/ThumbnailCache.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <filesystem>
#include <system_error>
#include <Editor/Core/Rigging/RigBuilder.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>

#include <algorithm>
#include <cfloat>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    StaticMeshComponentWidget::StaticMeshComponentWidget( const Assets::AssetManager* assetManager,
                                                          const ComponentEditContext* ctx )
         : ComponentWidget( "3D Model" ), m_AssetManager( assetManager ), m_Ctx( ctx )
    {
    }

    void StaticMeshComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        auto& staticMesh = entity.GetComponent<ECS::StaticMeshComponent>();

        Utils::ImGuiUtilities::PushID();

        // No local FramePadding override: the row height is a PANEL metric (ThemeManager), and a widget
        // that shrinks its own controls is exactly how the Details grid ends up with rows of three
        // different heights.
        Utils::ImGuiUtilities::ResetPropertyRows();
        Utils::ImGuiUtilities::BeginPropertyRow( "Mesh Type" );

        const char* meshTypes[] = { "Asset", "Primitive" };
        int currentType = staticMesh.Primitive.has_value() ? 1 : 0;
        if ( ImGui::Combo( "##MeshType", &currentType, meshTypes, IM_ARRAYSIZE( meshTypes ) ) )
        {
            if ( currentType == 0 )
                staticMesh.Primitive.reset();
            else
                staticMesh.Primitive = Geometry::PrimitiveType::Cube;
        }

        Utils::ImGuiUtilities::EndPropertyRow();

        // The preview is a THUMBNAIL on this row, not a section of its own: what the mesh looks like
        // belongs beside the slot that chooses it, the way UE draws an asset row.
        constexpr float kThumb   = 64.0f;
        const float     assetRow = std::max( kThumb, ImGui::GetFrameHeight() ) + ImGui::GetStyle().ItemSpacing.y;

        if ( !staticMesh.Primitive.has_value() )
        {
            Utils::ImGuiUtilities::BeginPropertyRow( "Asset", nullptr, assetRow );
            DrawMeshThumbnail( staticMesh, kThumb );

            std::string currentSelectionName = "Select Mesh";
            bool        emptySlot            = true;
            if ( staticMesh.MeshHandle )
            {
                auto meshAsset = m_AssetManager->FindByHandle<Assets::MeshAsset>( staticMesh.MeshHandle );
                if ( meshAsset )
                {
                    currentSelectionName = Common::Utils::FileSystem::GetFileName( meshAsset->GetMetadata().Filepath );
                    emptySlot = false;
                }
            }

            // A sunk asset slot, not a raised button: this row HOLDS a value (UE draws it the same way).
            if ( Utils::ImGuiUtilities::AssetSlot( "MeshSlot", currentSelectionName.c_str(), emptySlot ) )
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
            Utils::ImGuiUtilities::EndPropertyRow();
        }
        else
        {
            Utils::ImGuiUtilities::BeginPropertyRow( "Shape", nullptr, assetRow );
            DrawMeshThumbnail( staticMesh, kThumb );

            const char* shapes[] = { "Cube", "Sphere", "Pyramid", "Plane", "Cylinder", "Capsule" };
            int currentShape = (int)staticMesh.Primitive.value();
            if ( ImGui::Combo( "##Shape", &currentShape, shapes, IM_ARRAYSIZE( shapes ) ) )
            {
                staticMesh.Primitive = (Geometry::PrimitiveType)currentShape;
                // MeshECSSystem will handle the dynamic mesh generation/update
            }

            Utils::ImGuiUtilities::EndPropertyRow();
        }

        ShowMeshDetails( entity, scene, staticMesh );

        {
            static MaterialComponentWidget materialComponent( m_AssetManager );
            materialComponent.Render( entity, scene );
        }

        RenderRigging( entity, staticMesh );

        Utils::ImGuiUtilities::PopID();
    }

    void StaticMeshComponentWidget::DrawMeshThumbnail( const ECS::StaticMeshComponent& staticMesh,
                                                       float                           size ) const
    {
        // LIVE first: the panel lends one preview renderer, and it shows what this entity actually renders
        // — orbitable, and it follows a material edit while you drag the slider. It is safe again because
        // per-frame GPU state is stored per (frame x renderer slot) now, so a second renderer no longer
        // overwrites the viewport's camera, lights and shadows (Docs/RENDERER_FRAME_STATE.md).
        if ( m_Ctx && m_Ctx->DrawPreview( ImVec2( size, size ) ) )
        {
            Utils::ImGuiUtilities::Tooltip( "Live preview — drag to orbit, wheel to zoom" );
            ImGui::SameLine();
            return;
        }

        // No renderer lent (a panel that does not own one): fall back to the PNG the asset browser already
        // rendered. THE MESH first — this row is the mesh slot, and showing a material sphere where the
        // model belongs answers a question nobody asked. The material is only the last resort, for an
        // entity whose mesh has no thumbnail yet (a primitive).
        static ThumbnailCache s_Thumbnails;

        std::shared_ptr<Graphic::Image2D> thumb;
        if ( m_AssetManager )
        {
            std::string path;
            if ( staticMesh.MeshHandle )
            {
                if ( auto mesh = m_AssetManager->FindByHandle<Assets::MeshAsset>( staticMesh.MeshHandle ) )
                    path = mesh->GetMetadata().Filepath.generic_string();
            }
            if ( path.empty() && !staticMesh.MaterialSlots.empty() && staticMesh.MaterialSlots.front() )
            {
                if ( auto mat = m_AssetManager->FindByHandle<Assets::SurfaceMaterialAsset>(
                          staticMesh.MaterialSlots.front() ) )
                    path = mat->GetMetadata().Filepath.generic_string();
            }

            if ( !path.empty() )
            {
                std::error_code   ec;
                const std::string png = ThumbnailCache::DiskPath( path );
                if ( std::filesystem::exists( png, ec ) )
                    thumb = s_Thumbnails.Get( png );
            }
        }

        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 br( at.x + size, at.y + size );
        ImDrawList*  dl = ImGui::GetWindowDrawList();

        ImGui::Dummy( ImVec2( size, size ) );
        dl->AddRectFilled( at, br, IM_COL32( 15, 15, 15, 255 ), 2.0f );

        if ( thumb && m_Ctx && m_Ctx->UIHelper )
        {
            if ( const void* tex = m_Ctx->UIHelper->GetTextureID( thumb ) )
                dl->AddImageRounded( reinterpret_cast<ImTextureID>( const_cast<void*>( tex ) ),
                                     ImVec2( at.x + 1.0f, at.y + 1.0f ), ImVec2( br.x - 1.0f, br.y - 1.0f ),
                                     ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32_WHITE, 2.0f );
        }
        else
        {
            const char*  icon = ICON_MDI_CUBE_OUTLINE;
            const ImVec2 ts   = ImGui::CalcTextSize( icon );
            dl->AddText( ImVec2( at.x + ( size - ts.x ) * 0.5f, at.y + ( size - ts.y ) * 0.5f ),
                         ImGui::GetColorU32( ImGuiCol_TextDisabled ), icon );
        }
        dl->AddRect( at, br, ImGui::GetColorU32( ImGuiCol_Border ), 2.0f );
        Utils::ImGuiUtilities::Tooltip( "Thumbnail rendered by the asset browser" );

        ImGui::SameLine();
    }

    void StaticMeshComponentWidget::ShowMeshDetails( const ECS::Entity& entity, ::Desert::Core::Scene* scene,
                                                     const ECS::StaticMeshComponent& staticMesh ) const
    {
        MeshDetailsWidget::Context ctx;
        ctx.Entity    = &entity;
        ctx.Scene     = scene;
        ctx.ForcedLOD = staticMesh.ForcedLOD;
        ctx.LODBias   = staticMesh.LODBias;

        // The mesh that is ACTUALLY drawn — one shared resolver, so the panel, the viewport overlay and
        // the collider fit can never disagree about which mesh an entity shows.
        ctx.RuntimeMesh = ResolveDrawnMesh( entity );
        if ( staticMesh.MeshHandle )
            ctx.Asset = m_AssetManager->FindByHandle<Assets::MeshAsset>( staticMesh.MeshHandle );

        MeshDetailsWidget::Show( ctx );
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

        ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );
        if ( !Utils::ImGuiUtilities::SectionHeader( ICON_MDI_BONE "  Rigging (Skeleton)" ) )
            return;

        const bool riggingThis = RigBuilder::IsActive() && RigBuilder::Target() == uuid;

        ImGui::Indent( 6.0f );
        ImGui::Dummy( ImVec2( 0.0f, 2.0f ) );

        if ( !riggingThis )
        {
            if ( RigBuilder::IsActive() )
            {
                ImGui::TextColored( ThemeManager::GetWarningColor(),
                                    ICON_MDI_ALERT " Another mesh is being rigged." );
                ImGui::Dummy( ImVec2( 0.0f, 2.0f ) );
            }
            ImGui::PushTextWrapPos( 0.0f );
            ImGui::TextDisabled( "Place bones on this static mesh, then convert it to a skinned mesh with "
                                 "automatic vertex weights. Pose the bones afterwards in Skeleton Edit mode." );
            ImGui::PopTextWrapPos();
            ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );
            if ( Utils::ImGuiUtilities::AccentButton( ICON_MDI_BONE "  Add Skeleton / Rig this Mesh", 28.0f ) )
                RigBuilder::Begin( uuid, center );
            ImGui::Unindent( 6.0f );
            return;
        }

        const auto& bones = RigBuilder::Bones();
        const int   sel   = RigBuilder::SelectedBone();

        ImGui::TextDisabled( "BONES  (%d)", static_cast<int>( bones.size() ) );
        ImGui::BeginChild( "##rigBones", ImVec2( 0.0f, std::min( 140.0f, 8.0f + bones.size() * 20.0f ) ), true );
        for ( int i = 0; i < static_cast<int>( bones.size() ); ++i )
        {
            ImGui::PushID( i );
            std::string label = std::string( ICON_MDI_BONE "  " ) + bones[i].Name;
            if ( bones[i].Parent < 0 )
                label += "   (root)";
            if ( ImGui::Selectable( label.c_str(), i == sel ) )
                RigBuilder::SelectBone( i );
            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::Dummy( ImVec2( 0.0f, 2.0f ) );
        if ( sel >= 0 && sel < static_cast<int>( bones.size() ) )
        {
            glm::vec3 head = bones[sel].Head;
            ImGui::SetNextItemWidth( -1.0f );
            if ( ImGui::DragFloat3( "##head", &head.x, 0.01f, 0.0f, 0.0f, "%.3f" ) )
                RigBuilder::SetHead( sel, head );
            ImGui::SameLine( 0.0f, 0.0f );
        }

        if ( ImGui::Button( ICON_MDI_PLUS "  Add Child", ImVec2( ImGui::GetContentRegionAvail().x * 0.5f, 0 ) ) )
        {
            const glm::vec3 head = ( sel >= 0 ) ? bones[sel].Head + glm::vec3( 0.0f, 0.5f, 0.0f ) : center;
            RigBuilder::AddBone( sel, head );
        }
        ImGui::SameLine();
        if ( ImGui::Button( ICON_MDI_DELETE "  Delete", ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
            RigBuilder::DeleteBone( sel );

        ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );
        if ( Utils::ImGuiUtilities::AccentButton( ICON_MDI_RUN_FAST "  Convert to Skinned", 30.0f ) )
            RigBuilder::RequestConvert();
        if ( ImGui::Button( "Cancel", ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
            RigBuilder::Cancel();

        ImGui::Unindent( 6.0f );
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

    DESERT_REGISTER_CUSTOM_COMPONENT( ECS::StaticMeshComponent, "3D Model", false,
                                      ( []( ECS::Entity& e, ::Desert::Core::Scene* s,
                                            const ComponentEditContext& ctx )
                                        { StaticMeshComponentWidget( ctx.AssetMgr(), &ctx ).Render( e, s ); } ) )

} // namespace Desert::Editor

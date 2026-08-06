#include "MaterialsPanelComponent.hpp"
#include <Editor/Core/DragPayloads.hpp>
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <ImGui/imgui.h>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Constants.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Core/ThemeManager.hpp>

#include <Editor/Panels/PropertyEditor/PropertyEditorBuilder.hpp>
#include <Editor/Widgets/ThumbnailCache.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/LODSelection.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/Material/MaterialService.hpp>
#include <Engine/Runtime/Services/Shader/ShaderService.hpp>
#include <Engine/Graphic/Materials/DataDrivenMaterial.hpp>
#include <Editor/Import/TextureDnD.hpp>
#include <Engine/Assets/TextureAsset.hpp>

// rfl serialization environment (same as SurfaceMaterialAsset.cpp) — used to write a fresh
// material file with its stable GUID before the asset is created/registered.
#include <Engine/Core/Serialize/GLMReflect.hpp>
#include <Engine/Core/Serialize/CustomReflect.hpp>
#include <rflcpp/rfl/json.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
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

    MaterialComponentWidget::MaterialHost MaterialComponentWidget::HostOf( ECS::Entity& entity )
    {
        MaterialHost host;
        if ( entity.HasComponent<ECS::StaticMeshComponent>() )
        {
            auto& c               = entity.GetComponent<ECS::StaticMeshComponent>();
            host.Slots            = &c.MaterialSlots;
            host.RuntimeInstances = &c.RuntimeMaterialInstances;
            host.MeshHandle       = c.MeshHandle;
            host.Mesh             = c.RuntimeMesh ? static_cast<::Desert::Mesh*>( c.RuntimeMesh.get() )
                                                  : Runtime::ResourceRegistry::GetMeshService()->Get( c.MeshHandle );
        }
        else if ( entity.HasComponent<ECS::SkinnedMeshComponent>() )
        {
            auto& c               = entity.GetComponent<ECS::SkinnedMeshComponent>();
            host.Slots            = &c.MaterialSlots;
            host.RuntimeInstances = &c.RuntimeMaterialInstances;
            host.MeshHandle       = c.MeshHandle;
            host.Mesh             = c.RuntimeMesh ? static_cast<::Desert::Mesh*>( c.RuntimeMesh.get() )
                                                  : Runtime::ResourceRegistry::GetMeshService()->Get( c.MeshHandle );
        }
        return host;
    }

    void MaterialComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        const MaterialHost host = HostOf( entity );
        if ( !host.Slots )
            return;

        // A Shader Override component with a custom (non-PBR) shader takes this mesh off the PBR
        // path entirely — surface that here so the slots below don't look mysteriously dead.
        std::string overriddenBy;
        if ( entity.HasComponent<ECS::MaterialComponent>() )
        {
            const auto& matc = entity.GetComponent<ECS::MaterialComponent>();
            if ( !matc.ShaderName.empty() && matc.ShaderName != "StaticMeshPBR" &&
                 matc.ShaderName != "SkinnedMeshPBR" )
                overriddenBy = matc.ShaderName;
        }

        // NOTE: the per-entity "Runtime Override" param editor was removed — meshes get their look ONLY
        // from material-asset SLOTS now (UE-style). The MaterialComponent param channel survives purely as
        // the scripting (Lua setMaterialParam) + legacy-scene compat path; it is no longer editor-authored.

        RenderMaterialProperties( entity, host, overriddenBy );

        // Quick way back to the PBR path without hunting for the Shader Override section.
        if ( !overriddenBy.empty() )
        {
            if ( ImGui::Button( "Clear runtime override (use material slots)" ) )
            {
                auto& matc = entity.GetComponent<ECS::MaterialComponent>();
                matc.ShaderName.clear();
                matc.Params.clear();
                matc.Textures.clear();
            }
        }

        // Level of Detail + Rendering flags live on the STATIC mesh component only (a skinned mesh has no
        // LOD chain and no per-submesh visibility mask), so a skinned entity stops here — with its material
        // slots drawn, which is the part it was missing entirely.
        if ( !entity.HasComponent<ECS::StaticMeshComponent>() )
            return;

        auto&           materialComp = entity.GetComponent<ECS::StaticMeshComponent>();
        ::Desert::Mesh* lodMesh      = host.Mesh;

        if ( Utils::ImGuiUtilities::SectionHeader( ICON_MDI_LAYERS_TRIPLE "  Level of Detail", false ) )
        {
            size_t levels = 0;
            if ( lodMesh )
                for ( const auto& sm : lodMesh->GetSubmeshes() )
                    if ( sm.LODs.size() > levels )
                        levels = sm.LODs.size();

            if ( levels <= 1 )
            {
                ImGui::TextDisabled( lodMesh ? "No LOD chain (mesh too small / not generated)."
                                             : "No LOD chain (primitive meshes have a single level)." );
            }
            else
            {
                // Which level the current view resolves to — the renderer's own policy, so the marker
                // can't disagree with what is on screen.
                size_t active     = 0;
                bool   haveActive = false;
                if ( scene )
                {
                    if ( const auto& camera = scene->GetActiveCamera() )
                    {
                        active =
                             std::min<size_t>( Geometry::SelectLOD( entity.GetWorldTransform(),
                                                                    lodMesh->GetSubmeshes(), camera->GetPosition(),
                                                                    materialComp.ForcedLOD, materialComp.LODBias ),
                                               levels - 1 );
                        haveActive = true;
                    }
                }

                const auto& subs = lodMesh->GetSubmeshes();
                for ( size_t l = 0; l < levels; ++l )
                {
                    uint32_t tris = 0;
                    for ( const auto& sm : subs )
                    {
                        const size_t li = l < sm.LODs.size() ? l : sm.LODs.size() - 1;
                        tris += sm.LODs[li].IndexCount / 3;
                    }
                    if ( haveActive && l == active )
                        ImGui::BulletText( "LOD %zu \xE2\x80\x94 %u tris   (drawing)", l, tris );
                    else
                        ImGui::BulletText( "LOD %zu \xE2\x80\x94 %u tris", l, tris );
                }
            }

            // Shared property rows: label column + full-width control, same stripe and hover as every
            // other row in Details.
            Utils::ImGuiUtilities::ResetPropertyRows();

            const char* items[] = { "Auto (by distance)", "LOD 0", "LOD 1", "LOD 2", "LOD 3" };
            int         cur     = materialComp.ForcedLOD < 0 ? 0 : materialComp.ForcedLOD + 1;
            Utils::ImGuiUtilities::BeginPropertyRow( "Force LOD" );
            if ( ImGui::Combo( "##forcelod", &cur, items, 5 ) )
                materialComp.ForcedLOD = ( cur == 0 ) ? -1 : cur - 1;
            Utils::ImGuiUtilities::EndPropertyRow();

            Utils::ImGuiUtilities::BeginPropertyRow(
                 "LOD Bias",
                 "Shifts the automatic LOD pick (+coarser, -finer). No effect while a LOD is forced." );
            ImGui::SliderInt( "##lodbias", &materialComp.LODBias, -3, 3 );
            Utils::ImGuiUtilities::EndPropertyRow();
        }

        // Rendering: persistent outline + per-submesh visibility. Both write component fields the
        // renderer honours (MeshECSSystem ORs OutlineDraw into the outline flag / the HiddenSubmeshes
        // bitmask), so they take effect live — the LOD-style inline-control pattern applied to the mesh.
        if ( Utils::ImGuiUtilities::SectionHeader( ICON_MDI_EYE "  Rendering", false ) )
        {
            Utils::ImGuiUtilities::ResetPropertyRows();

            Utils::ImGuiUtilities::BeginPropertyRow(
                 "Draw Outline", "Always draw the outline for this mesh, even when it is not selected" );
            ImGui::Checkbox( "##outline", &materialComp.OutlineDraw );
            Utils::ImGuiUtilities::EndPropertyRow();

            Utils::ImGuiUtilities::BeginPropertyRow( "Cast Shadows",
                                                     "Skip this mesh in the shadow (depth) passes" );
            ImGui::Checkbox( "##castshadows", &materialComp.CastShadows );
            Utils::ImGuiUtilities::EndPropertyRow();

            Utils::ImGuiUtilities::BeginPropertyRow(
                 "Receive Shadows", "Sun (directional) shadows are not applied to this mesh when off" );
            ImGui::Checkbox( "##recvshadows", &materialComp.ReceiveShadows );
            Utils::ImGuiUtilities::EndPropertyRow();

            const size_t subCount = lodMesh ? lodMesh->GetSubmeshes().size() : 0;
            if ( subCount > 1 )
            {
                ImGui::Spacing();
                ImGui::TextDisabled( "Submesh visibility" );
                for ( size_t i = 0; i < subCount && i < 64; ++i )
                {
                    const uint64_t bit     = ( 1ull << i );
                    bool           visible = ( materialComp.HiddenSubmeshes & bit ) == 0;
                    const auto&    sm      = lodMesh->GetSubmeshes()[i];
                    const std::string label =
                         sm.Name.empty() ? ( "Submesh " + std::to_string( i ) ) : sm.Name;
                    ImGui::PushID( static_cast<int>( i ) );
                    if ( ImGui::Checkbox( label.c_str(), &visible ) )
                        materialComp.HiddenSubmeshes =
                             visible ? ( materialComp.HiddenSubmeshes & ~bit ) : ( materialComp.HiddenSubmeshes | bit );
                    ImGui::PopID();
                }
            }
        }
    }

    size_t MaterialComponentWidget::GetSubmeshCount( const MaterialHost& host ) const
    {
        // The ACTUAL submesh count of the mesh being rendered is the truth (the renderer maps
        // submesh i -> slot min(i, slots-1)).
        if ( host.Mesh && !host.Mesh->GetSubmeshes().empty() )
            return host.Mesh->GetSubmeshes().size();

        // Not built yet — fall back to the asset's imported material list.
        if ( host.MeshHandle )
        {
            if ( auto* meshAsset = Runtime::ResourceRegistry::GetMeshService()->GetAsset( host.MeshHandle ) )
            {
                const size_t count = meshAsset->GetMaterialHandles().size();
                if ( count > 0 )
                    return count;
            }
        }
        return 1; // single implicit slot when the mesh exposes none (e.g. primitives)
    }

    Assets::AssetHandle MaterialComponentWidget::CreateAndRegisterMaterial( const std::string& baseName )
    {
        if ( !m_AssetManager )
            return {};

        // Meaningful, human-readable filename: "M_<Entity>" (sanitized), then "M_<Entity>_1", ... —
        // first free index in the material folder. The name is ONLY a label: the stable identity
        // lives INSIDE the file (MaterialId), so renaming the entity or the file later breaks nothing.
        std::string base;
        base.reserve( baseName.size() );
        for ( const char c : baseName )
            base += ( std::isalnum( static_cast<unsigned char>( c ) ) || c == '_' || c == '-' ) ? c : '_';
        if ( base.empty() )
            base = "Material";

        const std::string         ext = Common::Constants::Extensions::MATERIAL_EXTENSION;
        const std::filesystem::path dir = Common::Constants::Path::MATERIAL_PATH;
        std::error_code             ec;
        std::filesystem::create_directories( dir, ec );

        std::filesystem::path path = dir / ( base + ext );
        for ( int n = 1; std::filesystem::exists( path, ec ); ++n )
            path = dir / ( base + "_" + std::to_string( n ) + ext );

        // Write the file FIRST (defaults + freshly stamped MaterialId), then create-with-load: the
        // asset adopts its stable in-file GUID as the internal handle during Load, so the handle the
        // AssetManager/MaterialService register under is the same one every future editor run gets.
        {
            Assets::MaterialData defaults;
            defaults.MaterialId = Common::UUID();
            Common::Utils::FileSystem::WriteContentToFile( path.generic_string(),
                                                           rfl::json::write( defaults ) );
        }

        auto asset = const_cast<Assets::AssetManager&>( *m_AssetManager )
                          .CreateAsset<Assets::SurfaceMaterialAsset>( Assets::AssetPriority::High,
                                                                  path.generic_string() );
        if ( !asset )
            return {};

        Runtime::ResourceRegistry::GetMaterialService()->Register( asset );
        return asset->GetMetadata().Handle;
    }

    Assets::AssetHandle
    MaterialComponentWidget::CreateAndRegisterMaterialInstance( const Assets::SurfaceMaterialAsset& parent )
    {
        if ( !m_AssetManager )
            return Common::UUID::Null();

        const std::string           ext = Common::Constants::Extensions::MATERIAL_EXTENSION;
        const std::filesystem::path dir = Common::Constants::Path::MATERIAL_PATH;
        std::error_code             ec;
        std::filesystem::create_directories( dir, ec );

        const std::string     base = std::filesystem::path( parent.GetMetadata().Filepath ).stem().string() + "_Inst";
        std::filesystem::path path = dir / ( base + ext );
        for ( int n = 1; std::filesystem::exists( path, ec ); ++n )
            path = dir / ( base + "_" + std::to_string( n ) + ext );

        {
            Assets::MaterialData data;
            data.MaterialId = Common::UUID();
            // Reference the parent by its STABLE in-file id (falls back to the asset handle,
            // which file materials adopt from that id anyway).
            data.ParentMaterialId = ( parent.Data().MaterialId && !parent.Data().MaterialId->IsNull() )
                                         ? *parent.Data().MaterialId
                                         : Common::UUID( static_cast<uint64_t>( parent.GetMetadata().Handle ) );
            Common::Utils::FileSystem::WriteContentToFile( path.generic_string(), rfl::json::write( data ) );
        }

        auto asset = const_cast<Assets::AssetManager&>( *m_AssetManager )
                          .CreateAsset<Assets::SurfaceMaterialAsset>( Assets::AssetPriority::High,
                                                                      path.generic_string() );
        if ( !asset )
            return Common::UUID::Null();

        // LAZY registration: an instance asset must never build a runtime Material of its own
        // (Get() resolves it to the base; CreateRuntimeInstance applies the overrides).
        Runtime::ResourceRegistry::GetMaterialService()->RegisterAsset( asset );
        return asset->GetMetadata().Handle;
    }

    void MaterialComponentWidget::MakeSlotExplicit( const MaterialHost& host, size_t slot )
    {
        auto& meshComp = *host.Slots;
        // Extend the slot array up to `slot` by repeating the last handle — exactly the renderer's
        // min(i, slots-1) mapping — so materializing a row never changes the rendered look. With no
        // slots at all the fill is Null (the engine default), same as what those rows showed before.
        while ( meshComp.size() <= slot )
            meshComp.push_back( meshComp.empty() ? Common::UUID::Null() : meshComp.back() );
    }

    void MaterialComponentWidget::AssignMaterialFromPath( const MaterialHost& host, size_t slot,
                                                          const std::string& assetPath )
    {
        if ( !m_AssetManager )
            return;

        auto asset = m_AssetManager->FindByPath<Assets::SurfaceMaterialAsset>( assetPath );
        if ( !asset )
            asset = const_cast<Assets::AssetManager&>( *m_AssetManager )
                         .CreateAsset<Assets::SurfaceMaterialAsset>( Assets::AssetPriority::High, assetPath );
        if ( !asset )
            return;

        const auto handle = asset->GetMetadata().Handle;
        if ( !Runtime::ResourceRegistry::GetMaterialService()->Get( handle ) )
            Runtime::ResourceRegistry::GetMaterialService()->Register( asset );

        if ( slot < host.Slots->size() )
            ( *host.Slots )[slot] = handle;
        else
            host.Slots->push_back( handle );

        // Force MeshECSSystem to rebuild the runtime instances (it only rebuilds on slot-count change,
        // so an in-place handle swap needs an explicit reset).
        host.Invalidate();
    }

    bool MaterialComponentWidget::DrawShaderPicker( Assets::SurfaceMaterialAsset& asset )
    {
        auto* shaderService = Runtime::ResourceRegistry::GetShaderService();
        if ( !shaderService )
            return false;

        // No hardcoded entries: the picker lists every Surface-domain shader from the service —
        // StaticMeshPBR (the standard shader with the batched backend) included, like any other.
        const std::string current = asset.Data().EffectiveShaderName();

        bool shaderChanged = false;
        if ( ImGui::BeginCombo( "Shader", current.c_str() ) )
        {
            for ( const auto& name : shaderService->GetAllNames() )
            {
                auto candidate = shaderService->GetByName( name );
                if ( !candidate ||
                     candidate->GetProgramMeta().Domain != ::Desert::Core::Formats::ShaderDomain::Surface )
                    continue;

                const bool selected = ( name == current );
                if ( ImGui::Selectable( name.c_str(), selected ) && !selected )
                {
                    // Params always belong to a shader's schema — a switch clears them; the
                    // schema editor reseeds defaults on the next draw.
                    asset.Data().ShaderName = name;
                    asset.Data().Params.clear();
                    asset.Data().Textures.clear();
                    shaderChanged = true;
                }
                if ( selected )
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return shaderChanged;
    }

    // Schema-driven editor for ANY material (PBR included — its schema lives in
    // StaticMeshPBR.shader like every other shader's). Two-column rows: the label cell never
    // overlaps the control, controls stretch to the full remaining width.
    // parentData/isInstance: material-INSTANCE mode — the schema comes from the parent's shader,
    // non-overridden rows display the PARENT's value, edits write overrides into the child, and
    // texture rows are read-only (per-instance texture descriptors are a v2).
    bool MaterialComponentWidget::DrawCustomShaderMaterial( Assets::SurfaceMaterialAsset& asset,
                                                            const Assets::MaterialData*   parentData,
                                                            bool                          isInstance )
    {
        auto*             shaderService = Runtime::ResourceRegistry::GetShaderService();
        const std::string shaderName    = parentData ? parentData->EffectiveShaderName()
                                                     : asset.Data().EffectiveShaderName();
        auto              shader        = shaderService ? shaderService->GetByName( shaderName ) : nullptr;
        if ( !shader )
        {
            ImGui::TextDisabled( "Shader '%s' not found", shaderName.c_str() );
            return false;
        }

        const auto& schema = shader->GetProgramMeta();
        if ( schema.Params.empty() )
        {
            ImGui::TextDisabled( "Shader exposes no properties" );
            return false;
        }

        auto& data = asset.Data();

        bool changed = false;

        if ( !ImGui::BeginTable( "##material_params", 2,
                                 ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings ) )
            return false;
        ImGui::TableSetupColumn( "label", ImGuiTableColumnFlags_WidthStretch, 0.38f );
        ImGui::TableSetupColumn( "control", ImGuiTableColumnFlags_WidthStretch, 0.62f );

        for ( const auto& p : schema.Params )
        {
            using W  = ::Desert::Core::Formats::ShaderParamWidget;
            using VT = ::Desert::Core::Formats::ShaderValueType;

            const char*       label    = p.DisplayName.empty() ? p.Name.c_str() : p.DisplayName.c_str();
            const std::string hiddenId = "##mp_" + p.Name; // control id; label lives in its own cell

            // Instance mode: a row with its own entry in the child IS an override — mark it.
            const bool overridden = isInstance && !p.IsTexture && asset.Data().FindParam( p.Name ) != nullptr;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if ( overridden )
                ImGui::TextColored( ImVec4( 1.0f, 0.85f, 0.4f, 1.0f ), "%s *", label );
            else
                ImGui::TextUnformatted( label );
            ImGui::TableNextColumn();
            ImGui::PushItemWidth( -FLT_MIN );

            if ( p.IsTexture )
            {
                if ( isInstance )
                {
                    // v1: textures always come from the parent (per-instance texture overrides
                    // need their own descriptor sets — the batched SSBO path can't carry them).
                    ImGui::TextDisabled( "from parent material" );
                    ImGui::PopItemWidth();
                    continue;
                }
                std::string disp = "<drop texture>";
                if ( const uint64_t h = data.GetTexture( p.Name ); h != 0 && m_AssetManager )
                {
                    if ( auto tex = m_AssetManager->FindByHandle<Assets::TextureAsset>( Common::UUID( h ) ) )
                    {
                        const auto& src  = tex->GetSourcePath();
                        const auto  path = !src.empty() ? src : tex->GetMetadata().Filepath.string();
                        disp             = std::filesystem::path( path ).filename().string();
                    }
                }

                ImGui::Button( ( disp + hiddenId ).c_str(), ImVec2( -FLT_MIN, 0.0f ) );
                if ( ImGui::BeginDragDropTarget() )
                {
                    if ( const ImGuiPayload* pl =
                              ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::TextureAsset ) )
                    {
                        const std::string path( static_cast<const char*>( pl->Data ),
                                                pl->DataSize > 0 ? pl->DataSize - 1 : 0 );
                        if ( m_AssetManager )
                        {
                            const auto resolved = ::Desert::Editor::TextureDnD::ResolveOrImport(
                                 const_cast<Assets::AssetManager&>( *m_AssetManager ), path );
                            if ( static_cast<uint64_t>( resolved ) != 0 )
                            {
                                data.SetTexture( p.Name, static_cast<uint64_t>( resolved ) );
                                changed = true;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopItemWidth();
                continue;
            }

            // Seed with: child override -> parent's effective value (instance mode) -> schema default.
            const glm::vec4 fallback = parentData ? parentData->GetParam( p.Name, p.Default ) : p.Default;
            glm::vec4       value    = data.GetParam( p.Name, fallback );
            bool            edited   = false;

            if ( p.Widget == W::Color )
            {
                edited = ( p.Type == VT::Float3 ) ? ImGui::ColorEdit3( hiddenId.c_str(), &value.x )
                                                  : ImGui::ColorEdit4( hiddenId.c_str(), &value.x );
            }
            else
            {
                const int comps = ( p.Type == VT::Float2 )   ? 2
                                  : ( p.Type == VT::Float3 ) ? 3
                                  : ( p.Type == VT::Float4 ) ? 4
                                                             : 1;
                if ( p.Min.has_value() && p.Max.has_value() )
                {
                    float mn = *p.Min, mx = *p.Max;
                    edited = ImGui::SliderScalarN( hiddenId.c_str(), ImGuiDataType_Float, &value.x, comps,
                                                   &mn, &mx );
                }
                else
                {
                    edited = ImGui::DragScalarN( hiddenId.c_str(), ImGuiDataType_Float, &value.x, comps, 0.01f );
                }
            }

            if ( edited )
            {
                data.SetParam( p.Name, value );
                changed = true;
            }
            ImGui::PopItemWidth();
        }

        ImGui::EndTable();
        return changed;
    }

    MaterialComponentWidget::SlotSwatch
    MaterialComponentWidget::BuildSlotSwatch( const Assets::SurfaceMaterialAsset& asset,
                                              const Assets::MaterialData*         parentData )
    {
        SlotSwatch swatch;

        auto*             shaderService = Runtime::ResourceRegistry::GetShaderService();
        const std::string shaderName =
             parentData ? parentData->EffectiveShaderName() : asset.Data().EffectiveShaderName();
        auto shader = shaderService ? shaderService->GetByName( shaderName ) : nullptr;
        if ( !shader )
            return swatch;

        using W = ::Desert::Core::Formats::ShaderParamWidget;

        const auto& data = asset.Data();
        for ( const auto& p : shader->GetProgramMeta().Params )
        {
            if ( p.IsTexture || p.Widget != W::Color )
                continue;

            // Same value resolution as the parameter rows: child override -> parent's effective value
            // (instance mode) -> schema default. A slot must never advertise a colour it doesn't render.
            const glm::vec4 fallback = parentData ? parentData->GetParam( p.Name, p.Default ) : p.Default;
            swatch.HasColor          = true;
            swatch.Color             = data.GetParam( p.Name, fallback );
            break; // the FIRST colour is the material's identity; later ones are accents
        }
        return swatch;
    }

    void MaterialComponentWidget::DrawSlotPreview( const Assets::SurfaceMaterialAsset* asset,
                                                   const SlotSwatch& swatch, float size )
    {
        // The rendered preview comes from the SHARED on-disk thumbnail cache the asset browser fills; a
        // material it has never shown falls back to the material's own colour (Details does no offscreen
        // rendering of its own — that stays one renderer per panel).
        std::shared_ptr<Graphic::Image2D> thumb;
        std::string                       path;
        if ( asset )
        {
            path = asset->GetMetadata().Filepath.generic_string();

            std::error_code   ec;
            const std::string png       = ThumbnailCache::DiskPath( path );
            bool              haveFresh = std::filesystem::exists( png, ec );
            if ( haveFresh )
            {
                // Edited material -> the cached PNG (and its decoded texture) are a lie. Same 3s margin the
                // asset browser uses: coarse filesystem timestamps otherwise report the source as newer
                // right after the PNG was written.
                const auto pngTime = std::filesystem::last_write_time( png, ec );
                const auto srcTime = std::filesystem::last_write_time( path, ec );
                if ( !ec && ( srcTime - pngTime ) > std::chrono::seconds( 3 ) )
                {
                    haveFresh = false;
                    m_Thumbnails.Invalidate( png );
                }
            }
            if ( haveFresh )
                thumb = m_Thumbnails.Get( png );
        }

        // An InvisibleButton rather than a Dummy: the preview is the row's drag-drop target, and a drop
        // target needs a real item to hang on.
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton( "##preview", ImVec2( size, size ) );

        const ImVec2 br( at.x + size, at.y + size );
        ImDrawList*  dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled( at, br, IM_COL32( 15, 15, 15, 255 ), 2.0f );

        if ( thumb && m_UIHelper )
        {
            if ( const void* tex = m_UIHelper->GetTextureID( thumb ) )
                dl->AddImageRounded( reinterpret_cast<ImTextureID>( const_cast<void*>( tex ) ),
                                     ImVec2( at.x + 1.0f, at.y + 1.0f ), ImVec2( br.x - 1.0f, br.y - 1.0f ),
                                     ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32_WHITE, 2.0f );
        }
        else if ( swatch.HasColor )
        {
            dl->AddRectFilled(
                 ImVec2( at.x + 1.0f, at.y + 1.0f ), ImVec2( br.x - 1.0f, br.y - 1.0f ),
                 ImGui::ColorConvertFloat4ToU32( ImVec4( swatch.Color.x, swatch.Color.y, swatch.Color.z, 1.0f ) ),
                 2.0f );
        }
        dl->AddRect( at, br, ImGui::GetColorU32( ImGuiCol_Border ), 2.0f );

        // UE underlines a slot's preview with a colour bar. Ours carries the material's identity colour,
        // so two slots with the same (or no) thumbnail are still telling apart.
        if ( swatch.HasColor )
        {
            const ImU32 col =
                 ImGui::ColorConvertFloat4ToU32( ImVec4( swatch.Color.x, swatch.Color.y, swatch.Color.z, 1.0f ) );
            dl->AddRectFilled( ImVec2( at.x + 1.0f, br.y - 3.0f ), ImVec2( br.x - 1.0f, br.y ), col );
        }

        if ( !path.empty() )
            Utils::ImGuiUtilities::Tooltip( path.c_str() );
    }

    std::string MaterialComponentWidget::SlotNameOf( const MaterialHost& host, size_t index ) const
    {
        if ( !host.Mesh || index >= host.Mesh->GetSubmeshes().size() )
            return {};
        return host.Mesh->GetSubmeshes()[index].Name;
    }

    MaterialComponentWidget::SlotAction MaterialComponentWidget::DrawSlotRow( const SlotRow& row,
                                                                              std::string&   droppedPath )
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        // 64px: big enough to recognise a material by its render, and the SAME box the skeletal-mesh slot
        // uses — one preview size across Details.
        constexpr float   kPreview   = 64.0f;
        const auto*       asset      = row.Asset;
        const bool        hasOwnSlot = row.HasOwnSlot;

        // The row is as tall as its content — the preview beside a two-line stack (asset field + action
        // strip) — so the grid's rules frame it instead of cutting through it.
        const float stack = ImGui::GetFrameHeight() * 2.0f + style.ItemSpacing.y;
        const float rowH  = std::max( kPreview, stack ) + style.ItemSpacing.y;

        Utils::ImGuiUtilities::PropertyRowBackground( rowH );

        ImGui::Columns( 2 );
        ImGui::SetColumnWidth( 0, Utils::ImGuiUtilities::PropertyLabelWidth() );
        ImGui::AlignTextToFramePadding();

        const std::string label = "Element " + std::to_string( row.Index );
        ImGui::TextUnformatted( label.c_str() );
        // The mesh's own name for the element, beside the index — UE's "Slot" field. The INDEX is what
        // the renderer maps materials by, so it stays the identity and the name is the hint.
        if ( !row.SlotName.empty() )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", row.SlotName.c_str() );
        }
        // An element without its own slot still renders SOMETHING — say where that look comes from
        // rather than leaving the row looking broken.
        if ( !hasOwnSlot )
            ImGui::TextDisabled( "%s", asset ? "inherited" : "default" );
        else if ( row.IsInstance && !row.ParentName.empty() )
            ImGui::TextDisabled( "instance of %s", row.ParentName.c_str() );

        ImGui::NextColumn();

        SlotAction action = SlotAction::None;

        const auto acceptDrop = [&droppedPath]()
        {
            if ( !ImGui::BeginDragDropTarget() )
                return;
            if ( const ImGuiPayload* p =
                      ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::MaterialAsset ) )
            {
                droppedPath.assign( static_cast<const char*>( p->Data ), p->DataSize > 0 ? p->DataSize - 1 : 0 );
            }
            ImGui::EndDragDropTarget();
        };

        DrawSlotPreview( asset, row.Swatch, kPreview );
        acceptDrop();

        ImGui::SameLine();
        ImGui::BeginGroup();

        const std::string name = asset ? std::filesystem::path( asset->GetMetadata().Filepath ).stem().string()
                                       : std::string( "None" );
        if ( Utils::ImGuiUtilities::AssetSlot( "slot", name.c_str(), asset == nullptr ) )
            action = SlotAction::Pick;
        acceptDrop();

        // A strip of flat icon actions, UE's row of small buttons under the asset field. Text buttons
        // here would each be a full-width bar and the slot list would stop reading as a list.
        const auto iconButton = []( const char* icon, const char* tip, bool active )
        {
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.0f, 0.0f, 0.0f, 0.0f ) );
            ImGui::PushStyleColor( ImGuiCol_Text, active ? ThemeManager::GetSelectedColor()
                                                         : ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
            const bool clicked = ImGui::Button( icon );
            ImGui::PopStyleColor( 2 );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "%s", tip );
            ImGui::SameLine( 0.0f, 2.0f );
            return clicked;
        };

        if ( asset && hasOwnSlot )
        {
            if ( iconButton( ICON_MDI_PENCIL, "Edit this material's parameters", row.Editing ) )
                action = SlotAction::ToggleEdit;
        }
        if ( !hasOwnSlot && asset )
        {
            if ( iconButton( ICON_MDI_LINK_VARIANT,
                             "Give this element its own slot (same material, look unchanged)", false ) )
                action = SlotAction::MakeExplicit;
        }
        if ( !asset )
        {
            if ( iconButton( ICON_MDI_PLUS_BOX, "Create a new material for this element", false ) )
                action = SlotAction::CreateMaterial;
        }
        if ( asset && hasOwnSlot && !row.IsInstance )
        {
            if ( iconButton( ICON_MDI_CONTENT_DUPLICATE,
                             "New child instance — override parameters without touching the parent", false ) )
                action = SlotAction::CreateInstance;
        }
        if ( asset && hasOwnSlot && row.IsInstance )
        {
            if ( iconButton( ICON_MDI_BACKUP_RESTORE, "Drop every override — back to the parent's values",
                             false ) )
                action = SlotAction::ResetOverrides;
        }
        if ( asset && hasOwnSlot )
        {
            if ( iconButton( ICON_MDI_CONTENT_SAVE, "Save this material asset", false ) )
                action = SlotAction::Save;
        }

        // Always submitted, so the trailing SameLine of the strip never dangles into the group's end.
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled( "%s", row.ShaderName.c_str() );

        ImGui::EndGroup();

        Utils::ImGuiUtilities::PropertyColumnRule();
        ImGui::Columns( 1 );
        return action;
    }

    void MaterialComponentWidget::RenderMaterialProperties( ECS::Entity& entity, const MaterialHost& host,
                                                            const std::string& overriddenByShader )
    {
        Utils::ImGuiUtilities::PushID();

        // When a runtime override (script) bypasses the slots: say so loudly and
        // start the section collapsed. Slots are the only AUTHORED source of truth.
        if ( !overriddenByShader.empty() )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetWarningColor() );
            ImGui::TextWrapped( "%s Runtime shader override active ('%s', set by a script) — "
                                "the material slots below are NOT used until it is cleared.",
                                ICON_MDI_ALERT, overriddenByShader.c_str() );
            ImGui::PopStyleColor();
        }

        // NOTE: there is deliberately NO per-frame param-override channel over the slots anymore.
        // Script writes go straight to the runtime instance; pre-build/legacy params are consumed
        // ONCE by MeshECSSystem at instance build. Slots are the single authored source of truth.

        // One row per submesh (plus any extra explicit slot) — the count belongs on the header, UE-style,
        // so a collapsed section still says how many elements this mesh has.
        const size_t      submeshCount = GetSubmeshCount( host );
        const size_t      rowCount     = std::max( submeshCount, host.Slots->size() );
        const std::string slotDetail   = std::to_string( rowCount ) + ( rowCount == 1 ? " element" : " elements" );

        const bool materialsOpen = Utils::ImGuiUtilities::SectionHeader(
             ICON_MDI_PALETTE "  Material Slots", overriddenByShader.empty(), slotDetail.c_str() );

        // Drop a .mat onto the Materials header (works open or collapsed): create slots up to the submesh
        // count if there are none, then assign the dropped material to EVERY slot.
        if ( ImGui::BeginDragDropTarget() )
        {
            if ( const ImGuiPayload* p = ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::MaterialAsset ) )
            {
                const std::string path( static_cast<const char*>( p->Data ),
                                        p->DataSize > 0 ? p->DataSize - 1 : 0 );
                const size_t      count = GetSubmeshCount( host );
                while ( host.Slots->size() < count )
                    host.Slots->push_back( Common::UUID::Null() );
                for ( size_t s = 0; s < host.Slots->size(); ++s )
                    AssignMaterialFromPath( host, s, path );
            }
            ImGui::EndDragDropTarget();
        }

        if ( materialsOpen )
        {
            // New materials are named after the entity ("M_<Tag>") — "Material_9" told nobody
            // anything. The filename is a label only (identity = in-file GUID), so a later entity
            // rename orphans nothing.
            const std::string matBaseName =
                 "M_" + ( entity.HasComponent<ECS::TagComponent>()
                               ? entity.GetComponent<ECS::TagComponent>().Tag
                               : std::string( "Material" ) );

            // UE-style element list: ALWAYS one row per submesh (plus any extra explicit slots).
            // A row without its own slot INHERITS the effective material the renderer actually uses
            // (submesh i -> slot min(i, slots-1), or the engine default when there are no slots) and
            // is shown greyed with a "Make Explicit" affordance — no slots are created behind the
            // user's back, and making a row explicit never changes the rendered look.
            Utils::ImGuiUtilities::ResetPropertyRows();

            for ( size_t i = 0; i < rowCount; ++i )
            {
                ImGui::PushID( static_cast<int>( i ) );

                const bool hasOwnSlot = i < host.Slots->size() && ( *host.Slots )[i];

                // The handle this row EFFECTIVELY renders with (mirrors the renderer's mapping).
                Assets::AssetHandle handle = Common::UUID::Null();
                if ( hasOwnSlot )
                    handle = ( *host.Slots )[i];
                else if ( !host.Slots->empty() )
                    handle = ( *host.Slots )[std::min( i, host.Slots->size() - 1 )];

                const auto asset = ( m_AssetManager && handle )
                                        ? m_AssetManager->FindByHandle<Assets::SurfaceMaterialAsset>( handle )
                                        : nullptr;

                // Instance parent, resolved BEFORE the header is drawn: the header's swatches must show
                // the values the slot actually renders with, which for an instance live in the parent.
                const bool isInstanceAsset = asset && asset->Data().IsInstance();
                std::shared_ptr<Assets::SurfaceMaterialAsset> parentAsset;
                std::string                                   parentName;
                if ( isInstanceAsset && m_AssetManager )
                {
                    const auto parentHandle =
                         Runtime::ResourceRegistry::GetMaterialService()->GetAssetHandleByExternal(
                              *asset->Data().ParentMaterialId );
                    if ( !parentHandle.IsNull() )
                        parentAsset = m_AssetManager->FindByHandle<Assets::SurfaceMaterialAsset>( parentHandle );
                    parentName =
                         parentAsset ? std::filesystem::path( parentAsset->GetMetadata().Filepath ).stem().string()
                                     : std::string( "<missing parent>" );
                }

                SlotSwatch swatch;
                if ( asset )
                    swatch = BuildSlotSwatch( *asset, parentAsset ? &parentAsset->Data() : nullptr );

                // The shader the slot RENDERS with: an instance's own ShaderName is empty and would read
                // as the engine default here, so it comes from the parent chain.
                const std::string shaderName = asset ? ( parentAsset ? parentAsset->Data().EffectiveShaderName()
                                                                     : asset->Data().EffectiveShaderName() )
                                                     : std::string( "Engine default material" );

                // Whether this row's parameter editor is folded open is UI state of the row — it lives in
                // ImGui's per-window storage, not in the component, which would carry it into the scene file.
                ImGuiStorage* storage = ImGui::GetStateStorage();
                const ImGuiID editId  = ImGui::GetID( "##editing" );
                bool          editing = storage->GetBool( editId, false );

                SlotRow row;
                row.Index      = i;
                row.Asset      = asset.get();
                row.Swatch     = swatch;
                row.SlotName   = SlotNameOf( host, i );
                row.ShaderName = shaderName;
                row.ParentName = parentName;
                row.HasOwnSlot = hasOwnSlot;
                row.IsInstance = isInstanceAsset;
                row.Editing    = editing;

                std::string      dropped;
                const SlotAction action = DrawSlotRow( row, dropped );

                // Drop an existing material asset on the row to assign it (creates the slot if needed).
                if ( !dropped.empty() )
                {
                    MakeSlotExplicit( host, i );
                    AssignMaterialFromPath( host, i, dropped );
                }

                switch ( action )
                {
                    case SlotAction::Pick:
                    {
                        ImGui::OpenPopup( "material_picker" );
                        break;
                    }
                    case SlotAction::ToggleEdit:
                    {
                        editing = !editing;
                        storage->SetBool( editId, editing );
                        break;
                    }
                    case SlotAction::MakeExplicit:
                    {
                        MakeSlotExplicit( host, i );
                        host.Invalidate();
                        break;
                    }
                    case SlotAction::CreateMaterial:
                    {
                        if ( const auto h = CreateAndRegisterMaterial( matBaseName ) )
                        {
                            MakeSlotExplicit( host, i );
                            ( *host.Slots )[i] = h;
                            host.Invalidate();
                        }
                        break;
                    }
                    case SlotAction::CreateInstance:
                    {
                        if ( asset )
                        {
                            if ( const auto h = CreateAndRegisterMaterialInstance( *asset ) )
                            {
                                ( *host.Slots )[i] = h;
                                host.Invalidate();
                            }
                        }
                        break;
                    }
                    case SlotAction::ResetOverrides:
                    {
                        if ( asset )
                        {
                            asset->Data().Params.clear();
                            asset->Data().Textures.clear();
                            Runtime::ResourceRegistry::GetMaterialService()->BumpInvalidationVersion();
                        }
                        break;
                    }
                    case SlotAction::Save:
                    {
                        if ( asset )
                        {
                            Common::Utils::FileSystem::WriteContentToFile( asset->GetMetadata().Filepath,
                                                                           asset->Save() );
                            // Drop ONLY this material's cached thumbnail so the asset browser re-renders it
                            // with the new look immediately (no waiting on the modtime check; others untouched).
                            std::error_code   ec;
                            const std::string png =
                                 ThumbnailCache::DiskPath( asset->GetMetadata().Filepath.generic_string() );
                            std::filesystem::remove( png, ec );
                            // ...and the copy this panel already decoded, or the slot preview would keep
                            // showing the old look after the PNG is regenerated.
                            m_Thumbnails.Invalidate( png );
                        }
                        break;
                    }
                    case SlotAction::None:
                        break;
                }

                // The picker the slot field's chevron promises. Assigning here is the same operation as a
                // drag-drop, so it goes through the same MakeSlotExplicit + assign pair.
                if ( ImGui::BeginPopup( "material_picker" ) )
                {
                    static ImGuiTextFilter materialFilter;
                    materialFilter.Draw( "##search", 200.0f );
                    ImGui::Separator();

                    if ( hasOwnSlot && ImGui::Selectable( "None (use the engine default)" ) )
                    {
                        ( *host.Slots )[i] = Common::UUID::Null();
                        host.Invalidate();
                    }

                    if ( m_AssetManager )
                    {
                        for ( const auto& [candidate, matAsset] :
                              m_AssetManager->FindAllByType<Assets::SurfaceMaterialAsset>() )
                        {
                            const std::string matName =
                                 std::filesystem::path( matAsset->GetMetadata().Filepath ).stem().string();
                            if ( !materialFilter.PassFilter( matName.c_str() ) )
                                continue;
                            if ( ImGui::Selectable( matName.c_str(), candidate == handle ) )
                            {
                                MakeSlotExplicit( host, i );
                                ( *host.Slots )[i] = candidate;
                                host.Invalidate();
                            }
                        }
                    }
                    ImGui::EndPopup();
                }

                // The parameter editor, folded under the row it belongs to (UE opens a separate Material
                // Editor window; a fold keeps the slot LIST readable without one). Inherited rows have no
                // editor on purpose — the material is authored on the row that owns it.
                if ( editing && asset && hasOwnSlot )
                {
                    ImGui::Indent( 12.0f );

                    // ── Unity-style: the shader lives inside the material (base assets only — an
                    // instance always renders with its parent chain's shader) ────────────
                    if ( !isInstanceAsset && DrawShaderPicker( *asset ) )
                    {
                        // A different shader means a different runtime material CLASS —
                        // rebuild it from the asset and refresh the entity's instances.
                        Runtime::ResourceRegistry::GetMaterialService()->Invalidate( handle );
                        host.Invalidate();
                    }

                    // ONE schema-driven editor for every shader — the PBR schema lives in
                    // StaticMeshPBR.shader like any other shader's (single material protocol).
                    const bool changed = DrawCustomShaderMaterial(
                         *asset, parentAsset ? &parentAsset->Data() : nullptr, isInstanceAsset );

                    // Live edit -> viewport.
                    if ( changed )
                    {
                        if ( isInstanceAsset )
                        {
                            // An instance has no runtime Material of its own — bump the stamp so
                            // every cached instance set rebuilds with the new overrides next tick.
                            Runtime::ResourceRegistry::GetMaterialService()->BumpInvalidationVersion();
                        }
                        else if ( auto* runtime = Runtime::ResourceRegistry::GetMaterialService()->Get( handle ) )
                        {
                            if ( auto* pbr = dynamic_cast<Graphic::StaticMaterialPBR*>( runtime ) )
                                Graphic::MaterialFactory::ApplyPBRAsset( *pbr, *asset );
                            else if ( auto* ddm = dynamic_cast<Graphic::DataDrivenMaterial*>( runtime ) )
                                Graphic::MaterialFactory::ApplyShaderAsset( *ddm, *asset );
                            // Base edits must also reach entities rendering through CHILD
                            // instances of this material (their instances cache override sets).
                            Runtime::ResourceRegistry::GetMaterialService()->BumpInvalidationVersion();
                        }
                    }

                    ImGui::Unindent( 12.0f );
                }
                else if ( hasOwnSlot && !asset )
                {
                    // Slot exists but its asset can't be resolved (deleted/missing file). Not a styling
                    // question: the row would otherwise look like an ordinary empty slot.
                    ImGui::Indent( 12.0f );
                    ImGui::TextDisabled( ICON_MDI_ALERT "  Material asset missing" );
                    ImGui::Unindent( 12.0f );
                }

                ImGui::PopID();
            }
        }

        Utils::ImGuiUtilities::PopID();
    }

} // namespace Desert::Editor

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

    void MaterialComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        auto& materialComp = entity.GetComponent<ECS::StaticMeshComponent>();

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

        RenderMaterialProperties( entity, materialComp, overriddenBy );

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

        // Level of Detail: the triangle count per LOD level + a per-mesh Force LOD override (Auto picks
        // by camera distance). Only meshes that carry a LOD chain (imported / high-poly) show levels.
        ::Desert::Mesh* lodMesh = nullptr;
        if ( materialComp.MeshHandle )
            lodMesh = Runtime::ResourceRegistry::GetMeshService()->Get( materialComp.MeshHandle );
        else if ( materialComp.RuntimeMesh )
            lodMesh = materialComp.RuntimeMesh.get();

        if ( ImGui::CollapsingHeader( "Level of Detail" ) )
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

            const char* items[] = { "Auto (by distance)", "LOD 0", "LOD 1", "LOD 2", "LOD 3" };
            int         cur     = materialComp.ForcedLOD < 0 ? 0 : materialComp.ForcedLOD + 1;
            ImGui::SetNextItemWidth( 180.0f );
            if ( ImGui::Combo( "Force LOD", &cur, items, 5 ) )
                materialComp.ForcedLOD = ( cur == 0 ) ? -1 : cur - 1;

            ImGui::SetNextItemWidth( 180.0f );
            ImGui::SliderInt( "LOD Bias", &materialComp.LODBias, -3, 3 );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Shifts the automatic LOD pick (+coarser, -finer). No effect while a LOD is forced." );
        }

        // Rendering: persistent outline + per-submesh visibility. Both write component fields the
        // renderer honours (MeshECSSystem ORs OutlineDraw into the outline flag / the HiddenSubmeshes
        // bitmask), so they take effect live — the LOD-style inline-control pattern applied to the mesh.
        if ( ImGui::CollapsingHeader( "Rendering" ) )
        {
            ImGui::Checkbox( "Draw outline", &materialComp.OutlineDraw );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Always draw the outline for this mesh, even when it is not selected" );

            ImGui::Checkbox( "Cast Shadows", &materialComp.CastShadows );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Skip this mesh in the shadow (depth) passes" );

            ImGui::Checkbox( "Receive Shadows", &materialComp.ReceiveShadows );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Sun (directional) shadows are not applied to this mesh when off" );

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

    size_t MaterialComponentWidget::GetSubmeshCount( const ECS::StaticMeshComponent& meshComp ) const
    {
        // The ACTUAL submesh count of the mesh being rendered is the truth (the renderer maps
        // submesh i -> slot min(i, slots-1)): edited RuntimeMesh first, then the built mesh.
        if ( meshComp.RuntimeMesh && !meshComp.RuntimeMesh->GetSubmeshes().empty() )
            return meshComp.RuntimeMesh->GetSubmeshes().size();

        if ( meshComp.MeshHandle )
        {
            if ( auto* mesh = Runtime::ResourceRegistry::GetMeshService()->Get( meshComp.MeshHandle ) )
                if ( !mesh->GetSubmeshes().empty() )
                    return mesh->GetSubmeshes().size();

            // Not built yet — fall back to the asset's imported material list.
            if ( auto* meshAsset = Runtime::ResourceRegistry::GetMeshService()->GetAsset( meshComp.MeshHandle ) )
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

    void MaterialComponentWidget::MakeSlotExplicit( ECS::StaticMeshComponent& meshComp, size_t slot )
    {
        // Extend the slot array up to `slot` by repeating the last handle — exactly the renderer's
        // min(i, slots-1) mapping — so materializing a row never changes the rendered look. With no
        // slots at all the fill is Null (the engine default), same as what those rows showed before.
        while ( meshComp.MaterialSlots.size() <= slot )
            meshComp.MaterialSlots.push_back( meshComp.MaterialSlots.empty()
                                                   ? Common::UUID::Null()
                                                   : meshComp.MaterialSlots.back() );
    }

    void MaterialComponentWidget::AssignMaterialFromPath( ECS::StaticMeshComponent& meshComp, size_t slot,
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

        if ( slot < meshComp.MaterialSlots.size() )
            meshComp.MaterialSlots[slot] = handle;
        else
            meshComp.MaterialSlots.push_back( handle );

        // Force MeshECSSystem to rebuild the runtime instances (it only rebuilds on slot-count change,
        // so an in-place handle swap needs an explicit reset).
        meshComp.RuntimeMaterialInstances.clear();
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

    void MaterialComponentWidget::DrawSwatchInHeader( const SlotSwatch& swatch )
    {
        if ( !swatch.HasColor )
            return;

        // Painted straight into the header bar's rect: an interactive item here would fight the tree
        // node for the click and the material drag-drop target. Just the colour — parameter VALUES belong
        // in the editor below, not stamped onto a header where they cannot be changed.
        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        const float  h  = std::max( ( mx.y - mn.y ) - 8.0f, 6.0f );
        const float  y  = mn.y + ( ( mx.y - mn.y ) - h ) * 0.5f;
        if ( ( mx.x - mn.x ) < h + 80.0f ) // no room next to the label -> skip
            return;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float x  = mx.x - 6.0f - h;
        const ImU32 col =
             ImGui::ColorConvertFloat4ToU32( ImVec4( swatch.Color.x, swatch.Color.y, swatch.Color.z, 1.0f ) );
        dl->AddRectFilled( ImVec2( x, y ), ImVec2( x + h, y + h ), col, 2.0f );
        dl->AddRect( ImVec2( x, y ), ImVec2( x + h, y + h ), IM_COL32( 0, 0, 0, 120 ), 2.0f );
    }

    void MaterialComponentWidget::DrawSlotIdentityCard( const Assets::SurfaceMaterialAsset& asset,
                                                        const SlotSwatch&                   swatch,
                                                        const Assets::MaterialData*         parentData,
                                                        const std::string&                  parentName )
    {
        // UE's material slot: a framed thumbnail and the asset's identity beside it. Deliberately NO
        // parameter readouts — metallic / roughness are editable fields a few rows below, and a number
        // you cannot change is noise on a row whose whole job is to let you RECOGNISE the slot.
        constexpr float   kThumb = 64.0f;
        const std::string path   = asset.GetMetadata().Filepath.generic_string();
        const std::string name   = std::filesystem::path( path ).stem().string();

        // The rendered preview comes from the SHARED on-disk thumbnail cache the asset browser fills; a
        // material it has never shown falls back to the material's own colour (Details does no offscreen
        // rendering of its own — that stays one renderer per panel).
        std::shared_ptr<Graphic::Image2D> thumb;
        std::error_code                   ec;
        const std::string                 png       = ThumbnailCache::DiskPath( path );
        bool                              haveFresh = std::filesystem::exists( png, ec );
        if ( haveFresh )
        {
            // Edited material -> the cached PNG (and its decoded texture) are a lie. Same 3s margin the
            // asset browser uses: coarse filesystem timestamps otherwise report the source as newer right
            // after the PNG was written.
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

        // Drawn by hand so the rendered thumbnail and the colour fallback share one frame.
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 br( at.x + kThumb, at.y + kThumb );
        ImDrawList*  dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled( at, br, IM_COL32( 28, 30, 34, 255 ), 3.0f );

        if ( thumb && m_UIHelper )
        {
            if ( const void* tex = m_UIHelper->GetTextureID( thumb ) )
                dl->AddImageRounded( reinterpret_cast<ImTextureID>( const_cast<void*>( tex ) ),
                                     ImVec2( at.x + 1.0f, at.y + 1.0f ), ImVec2( br.x - 1.0f, br.y - 1.0f ),
                                     ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32_WHITE, 3.0f );
        }
        else if ( swatch.HasColor )
        {
            dl->AddRectFilled(
                 ImVec2( at.x + 1.0f, at.y + 1.0f ), ImVec2( br.x - 1.0f, br.y - 1.0f ),
                 ImGui::ColorConvertFloat4ToU32( ImVec4( swatch.Color.x, swatch.Color.y, swatch.Color.z, 1.0f ) ),
                 3.0f );
        }
        dl->AddRect( at, br, IM_COL32( 70, 74, 84, 255 ), 3.0f );
        ImGui::Dummy( ImVec2( kThumb, kThumb ) );
        Utils::ImGuiUtilities::Tooltip( path.c_str() );

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted( name.c_str() );
        // An instance renders with its parent chain's shader — its own ShaderName is empty and would read
        // as the default here.
        ImGui::TextDisabled( "%s", parentData ? parentData->EffectiveShaderName().c_str()
                                              : asset.Data().EffectiveShaderName().c_str() );
        if ( !parentName.empty() )
            ImGui::TextDisabled( "Instance of: %s", parentName.c_str() );
        ImGui::EndGroup();
        ImGui::Spacing();
    }

    void MaterialComponentWidget::RenderMaterialProperties( ECS::Entity&              entity,
                                                            ECS::StaticMeshComponent& meshComp,
                                                            const std::string&        overriddenByShader )
    {
        Utils::ImGuiUtilities::PushID();
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );

        // When a runtime override (script) bypasses the slots: say so loudly and
        // start the section collapsed. Slots are the only AUTHORED source of truth.
        if ( !overriddenByShader.empty() )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.75f, 0.2f, 1.0f ) );
            ImGui::TextWrapped( "%s Runtime shader override active ('%s', set by a script) — "
                                "the material slots below are NOT used until it is cleared.",
                                ICON_MDI_ALERT, overriddenByShader.c_str() );
            ImGui::PopStyleColor();
        }

        // NOTE: there is deliberately NO per-frame param-override channel over the slots anymore.
        // Script writes go straight to the runtime instance; pre-build/legacy params are consumed
        // ONCE by MeshECSSystem at instance build. Slots are the single authored source of truth.

        ImGuiTreeNodeFlags materialsFlags = ImGuiTreeNodeFlags_Framed;
        if ( overriddenByShader.empty() )
            materialsFlags |= ImGuiTreeNodeFlags_DefaultOpen;

        const bool materialsOpen = ImGui::TreeNodeEx( "Materials", materialsFlags );

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
            const size_t submeshCount = GetSubmeshCount( meshComp );
            const size_t rowCount     = std::max( submeshCount, meshComp.MaterialSlots.size() );

            for ( size_t i = 0; i < rowCount; ++i )
            {
                ImGui::PushID( static_cast<int>( i ) );

                const bool hasOwnSlot =
                     i < meshComp.MaterialSlots.size() && meshComp.MaterialSlots[i];

                // The handle this row EFFECTIVELY renders with (mirrors the renderer's mapping).
                Assets::AssetHandle handle        = Common::UUID::Null();
                size_t              inheritedFrom = i;
                if ( hasOwnSlot )
                    handle = meshComp.MaterialSlots[i];
                else if ( !meshComp.MaterialSlots.empty() )
                {
                    inheritedFrom = std::min( i, meshComp.MaterialSlots.size() - 1 );
                    handle        = meshComp.MaterialSlots[inheritedFrom];
                }

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

                std::string title = "Element " + std::to_string( i );
                if ( asset )
                    title += "  \xE2\x80\x94  " +
                             std::filesystem::path( asset->GetMetadata().Filepath ).stem().string();
                if ( !hasOwnSlot )
                    title += handle ? "  (inherited)" : "  (default)";
                // Stable node id: the label now carries the material name, and an id that changes on
                // assignment would reset the row's expand state every time a slot is filled.
                title += "##element";

                if ( !hasOwnSlot )
                    ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
                const bool nodeOpen = ImGui::TreeNodeEx(
                    title.c_str(), ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen );
                if ( !hasOwnSlot )
                    ImGui::PopStyleColor();

                // Identify the slot without opening it: the material's own colour, painted into the header
                // bar (no item, so the row keeps its click/drop behaviour).
                if ( asset )
                    DrawSwatchInHeader( swatch );

                // Drop an existing material asset onto this row to assign it (creates the slot if needed).
                if ( ImGui::BeginDragDropTarget() )
                {
                    if ( const ImGuiPayload* p = ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::MaterialAsset ) )
                    {
                        const std::string path( static_cast<const char*>( p->Data ),
                                                p->DataSize > 0 ? p->DataSize - 1 : 0 );
                        MakeSlotExplicit( meshComp, i );
                        AssignMaterialFromPath( meshComp, i, path );
                    }
                    ImGui::EndDragDropTarget();
                }

                if ( nodeOpen )
                {
                    if ( !hasOwnSlot )
                    {
                        // Inherited/default row: no inline editor (the material is authored on the row it
                        // belongs to) — say where the look comes from and offer to own it.
                        if ( asset )
                        {
                            const std::string matName =
                                 std::filesystem::path( asset->GetMetadata().Filepath ).stem().string();
                            ImGui::TextDisabled( "Uses Element %zu's material: %s", inheritedFrom,
                                                 matName.c_str() );
                        }
                        else
                        {
                            ImGui::TextDisabled( "Uses the engine default material" );
                        }

                        if ( handle )
                        {
                            if ( ImGui::Button( "Make Explicit", ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
                            {
                                MakeSlotExplicit( meshComp, i );
                                meshComp.RuntimeMaterialInstances.clear();
                            }
                            if ( ImGui::IsItemHovered() )
                                ImGui::SetTooltip( "Give this element its own slot (same material, look unchanged) "
                                                   "so it can be assigned/edited independently" );
                        }
                        else if ( ImGui::Button( "Create Material", ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
                        {
                            if ( const auto h = CreateAndRegisterMaterial( matBaseName ) )
                            {
                                MakeSlotExplicit( meshComp, i );
                                meshComp.MaterialSlots[i] = h;
                                meshComp.RuntimeMaterialInstances.clear();
                            }
                        }
                    }
                    else if ( asset )
                    {
                        // Material INSTANCE asset (UE model): shader + non-overridden params come
                        // from the parent chain; this editor writes ONLY overrides into the child.
                        // What this slot IS, before the parameter grid: preview, name, shader, swatches.
                        DrawSlotIdentityCard( *asset, swatch, parentAsset ? &parentAsset->Data() : nullptr,
                                              parentName );

                        // ── Unity-style: the shader lives inside the material (base assets only —
                        // an instance always renders with its parent chain's shader) ────────────
                        if ( !isInstanceAsset && DrawShaderPicker( *asset ) )
                        {
                            // A different shader means a different runtime material CLASS —
                            // rebuild it from the asset and refresh the entity's instances.
                            Runtime::ResourceRegistry::GetMaterialService()->Invalidate( handle );
                            meshComp.RuntimeMaterialInstances.clear();
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

                        if ( ImGui::Button( "Save", ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
                        {
                            Common::Utils::FileSystem::WriteContentToFile( asset->GetMetadata().Filepath,
                                                                           asset->Save() );
                            // Drop ONLY this material's cached thumbnail so the asset browser re-renders it
                            // with the new look immediately (no waiting on the modtime check; others untouched).
                            std::error_code   ec;
                            const std::string png =
                                 ThumbnailCache::DiskPath( asset->GetMetadata().Filepath.generic_string() );
                            std::filesystem::remove( png, ec );
                            // ...and the copy this panel already decoded, or the slot card would keep
                            // showing the old look after the PNG is regenerated.
                            m_Thumbnails.Invalidate( png );
                        }

                        if ( isInstanceAsset )
                        {
                            if ( ImGui::Button( "Reset Overrides",
                                                ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
                            {
                                asset->Data().Params.clear();
                                asset->Data().Textures.clear();
                                Runtime::ResourceRegistry::GetMaterialService()->BumpInvalidationVersion();
                            }
                            if ( ImGui::IsItemHovered() )
                                ImGui::SetTooltip( "Drop every override — back to the parent material's values" );
                        }
                        else
                        {
                            if ( ImGui::Button( "Create Material Instance",
                                                ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
                            {
                                if ( const auto h = CreateAndRegisterMaterialInstance( *asset ) )
                                {
                                    meshComp.MaterialSlots[i] = h;
                                    meshComp.RuntimeMaterialInstances.clear();
                                }
                            }
                            if ( ImGui::IsItemHovered() )
                                ImGui::SetTooltip( "New child asset inheriting this material — override "
                                                   "params per-object without touching the parent" );
                        }
                    }
                    else
                    {
                        // Slot exists but its asset can't be resolved (deleted/missing file).
                        ImGui::TextDisabled( "Material asset missing" );
                        if ( ImGui::Button( "Create Material",
                                            ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
                        {
                            if ( const auto h = CreateAndRegisterMaterial( matBaseName ) )
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

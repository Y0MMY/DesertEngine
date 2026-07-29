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
#include <Engine/Geometry/DynamicMesh.hpp>
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
                const auto& subs = lodMesh->GetSubmeshes();
                for ( size_t l = 0; l < levels; ++l )
                {
                    uint32_t tris = 0;
                    for ( const auto& sm : subs )
                    {
                        const size_t li = l < sm.LODs.size() ? l : sm.LODs.size() - 1;
                        tris += sm.LODs[li].IndexCount / 3;
                    }
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

    void MaterialComponentWidget::ClearPBRParamOverrides( ECS::Entity& entity )
    {
        if ( !entity.HasComponent<ECS::MaterialComponent>() )
            return;
        auto& matc = entity.GetComponent<ECS::MaterialComponent>();
        if ( matc.ShaderName.empty() || matc.ShaderName == "StaticMeshPBR" )
        {
            matc.Params.clear();
            matc.Textures.clear();
        }
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
    bool MaterialComponentWidget::DrawCustomShaderMaterial( Assets::SurfaceMaterialAsset& asset )
    {
        auto*             shaderService = Runtime::ResourceRegistry::GetShaderService();
        const std::string shaderName    = asset.Data().EffectiveShaderName();
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

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( label );
            ImGui::TableNextColumn();
            ImGui::PushItemWidth( -FLT_MIN );

            if ( p.IsTexture )
            {
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

            // Seed the canon entry with the schema default the first time the param shows up.
            glm::vec4 value = data.GetParam( p.Name, p.Default );
            bool      edited = false;

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

        // PBR-channel param overrides (script / legacy scene / startup demo) are re-applied over
        // slot 0 EVERY frame by MeshECSSystem — slot edits to those params silently don't show.
        // Surface the state with a one-click way back to pure slot authoring.
        if ( overriddenByShader.empty() && entity.HasComponent<ECS::MaterialComponent>() )
        {
            const auto& matc = entity.GetComponent<ECS::MaterialComponent>();
            if ( ( matc.ShaderName.empty() || matc.ShaderName == "StaticMeshPBR" ) && !matc.Params.empty() )
            {
                ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.75f, 0.2f, 1.0f ) );
                ImGui::TextWrapped( "%s %zu runtime param override(s) (script / legacy scene) are applied "
                                    "over the material slots each frame — matching slot params won't "
                                    "take effect until cleared.",
                                    ICON_MDI_ALERT, matc.Params.size() );
                ImGui::PopStyleColor();
                if ( ImGui::Button( "Clear Param Overrides" ) )
                    ClearPBRParamOverrides( entity );
            }
        }

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
                ClearPBRParamOverrides( entity ); // the explicit assignment must actually show
            }
            ImGui::EndDragDropTarget();
        }

        if ( materialsOpen )
        {
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

                std::string title = "Element " + std::to_string( i );
                if ( !hasOwnSlot )
                    title += handle ? "  (inherited)" : "  (default)";

                if ( !hasOwnSlot )
                    ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
                const bool nodeOpen = ImGui::TreeNodeEx(
                    title.c_str(), ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen );
                if ( !hasOwnSlot )
                    ImGui::PopStyleColor();

                // Drop an existing material asset onto this row to assign it (creates the slot if needed).
                if ( ImGui::BeginDragDropTarget() )
                {
                    if ( const ImGuiPayload* p = ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::MaterialAsset ) )
                    {
                        const std::string path( static_cast<const char*>( p->Data ),
                                                p->DataSize > 0 ? p->DataSize - 1 : 0 );
                        MakeSlotExplicit( meshComp, i );
                        AssignMaterialFromPath( meshComp, i, path );
                        ClearPBRParamOverrides( entity ); // the explicit assignment must actually show
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
                            if ( const auto h = CreateAndRegisterMaterial() )
                            {
                                MakeSlotExplicit( meshComp, i );
                                meshComp.MaterialSlots[i] = h;
                                meshComp.RuntimeMaterialInstances.clear();
                                ClearPBRParamOverrides( entity );
                            }
                        }
                    }
                    else if ( asset )
                    {
                        // ── Unity-style: the shader lives inside the material ──────────────
                        if ( DrawShaderPicker( *asset ) )
                        {
                            // A different shader means a different runtime material CLASS —
                            // rebuild it from the asset and refresh the entity's instances.
                            Runtime::ResourceRegistry::GetMaterialService()->Invalidate( handle );
                            meshComp.RuntimeMaterialInstances.clear();
                        }

                        // ONE schema-driven editor for every shader — the PBR schema lives in
                        // StaticMeshPBR.shader like any other shader's (single material protocol).
                        const bool changed = DrawCustomShaderMaterial( *asset );

                        // Live edit -> viewport: re-apply the canon onto the slot's runtime material.
                        if ( changed )
                        {
                            if ( auto* runtime = Runtime::ResourceRegistry::GetMaterialService()->Get( handle ) )
                            {
                                if ( auto* pbr = dynamic_cast<Graphic::StaticMaterialPBR*>( runtime ) )
                                    Graphic::MaterialFactory::ApplyPBRAsset( *pbr, *asset );
                                else if ( auto* ddm = dynamic_cast<Graphic::DataDrivenMaterial*>( runtime ) )
                                    Graphic::MaterialFactory::ApplyShaderAsset( *ddm, *asset );
                            }
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
                        // Slot exists but its asset can't be resolved (deleted/missing file).
                        ImGui::TextDisabled( "Material asset missing" );
                        if ( ImGui::Button( "Create Material",
                                            ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
                        {
                            if ( const auto h = CreateAndRegisterMaterial() )
                            {
                                meshComp.MaterialSlots[i] = h;
                                meshComp.RuntimeMaterialInstances.clear();
                                ClearPBRParamOverrides( entity );
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

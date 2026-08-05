// Reflected component editors: the full Details UI is auto-built from each data block's REFLECT()
// metadata (PropertyEditorBuilder) — no widget class, no edit to ComponentEditor. To expose a new
// reflected component in the editor, copy one line below.

#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>
#include <Editor/Panels/UI/UIAnchorControls.hpp>
#include <Editor/Core/DragPayloads.hpp>

#include <Engine/ECS/Components.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/Font/FontService.hpp>
#include <Engine/Graphic/Shader.hpp>
#include <Editor/Import/MeshDnD.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <Engine/Core/Formats/ShaderProgramMeta.hpp>

#include <ImGui/imgui.h>
#include <glm/glm.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Import/TextureDnD.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Core/Serialize/ComponentRegistry.hpp>
#include <Engine/Scripting/ScriptEngine.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Constants.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>

DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::DirectionLightComponent, Data, "DirectionalLightData",
                                     "Directional Light" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::PointLightComponent, Data, "PointLightData", "Point Light" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::SpotLightComponent, Data, "SpotLightData", "Spot Light" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::CameraComponent, Data, "CameraData", "Camera" )
// Terrain is a CUSTOM entry: reflected TerrainData UI + the terrain MATERIAL editor (terrain
// has no mesh material slots, so its shader/params live on the entity's MaterialComponent —
// edited HERE, inside the Terrain section, not as a separate confusing component).
// See MakeTerrainEntry below.
// Collider is registered as a CUSTOM component below (auto-fit to mesh bounds on add) instead of the
// plain reflected one-liner — see MakeColliderEntry.
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::RigidBodyComponent, Data, "RigidBodyData", "Rigid Body" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::CharacterControllerComponent, Data, "CharacterControllerData",
                                     "Character Controller" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::AudioSourceComponent, Data, "AudioSourceData", "Audio Source" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::ParticleEmitterComponent, Data, "ParticleEmitterData",
                                     "Particle Emitter" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UICanvasComponent, Data, "UICanvasData", "UI Canvas" )
// UI Layout is a CUSTOM entry (not the reflected one-liner) so the Details panel gets Unity-style anchor
// presets ("Fill / Match Parent" + a 4x4 grid) above the raw anchor/offset fields. See MakeUILayoutEntry.
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIPanelComponent, Data, "UIPanelData", "UI Panel" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UITextComponent2D, Data, "UITextData", "UI Text" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIButtonComponent, Data, "UIButtonData", "UI Button" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIIconComponent, Data, "UIIconData", "UI Icon" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIBindingComponent, Data, "UIBindingData", "UI Binding" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIScreenComponent, Data, "UIScreenData", "UI Screen" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIScreenStackComponent, Data, "UIScreenStackData",
                                     "UI Screen Stack" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UITweenComponent, Data, "UITweenData", "UI Tween" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIPointerEventsComponent, Data, "UIPointerEventsData",
                                     "UI Pointer Events" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIDraggableComponent, Data, "UIDraggableData", "UI Draggable" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIDropTargetComponent, Data, "UIDropTargetData",
                                     "UI Drop Target" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIImageComponent, Data, "UIImageData", "UI Image" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UILayoutGroupComponent, Data, "UILayoutGroupData",
                                     "UI Layout Group" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIProgressBarComponent, Data, "UIProgressBarData",
                                     "UI Progress Bar" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIToggleComponent, Data, "UIToggleData", "UI Toggle" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UISliderComponent, Data, "UISliderData", "UI Slider" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIScrollViewComponent, Data, "UIScrollViewData",
                                     "UI Scroll View" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIInputFieldComponent, Data, "UIInputFieldData",
                                     "UI Input Field" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIDropdownComponent, Data, "UIDropdownData", "UI Dropdown" )

namespace Desert::Editor
{
    // Terrain material editor. TERRAIN has no mesh material slots, so its shader + params are
    // authored on the entity's MaterialComponent — the ONE remaining authored use of that
    // component (mesh entities author materials in their slots; there the component is only a
    // runtime override channel for scripts). Schema-driven from the Terrain-domain shader, laid
    // out as a two-column table (label cell never overlaps the control).
    static void DrawTerrainMaterialWidget( ::Desert::ECS::Entity&          entity,
                                           ::Desert::Assets::AssetManager* assetMgr )
    {
        namespace ImGui = ::ImGui;
        auto& mat       = entity.GetComponent<::Desert::ECS::MaterialComponent>();

        auto* shaderService = ::Desert::Runtime::ResourceRegistry::GetShaderService();
        if ( !shaderService )
            return;

        if ( !entity.HasComponent<::Desert::ECS::TerrainComponent>() )
        {
            // Not terrain: the component only exists as a runtime/script/legacy override here.
            ImGui::TextWrapped( "Runtime shader override ('%s'). Authored materials live in the mesh's "
                                "PBR Materials slots.",
                                mat.ShaderName.empty() ? "<none>" : mat.ShaderName.c_str() );
            if ( ImGui::Button( "Clear override (use material slots)" ) )
            {
                mat.ShaderName.clear();
                mat.Params.clear();
                mat.Textures.clear();
            }
            return;
        }

        // --- Terrain-domain shader picker ---
        const std::string preview = mat.ShaderName.empty() ? "<none>" : mat.ShaderName;
        if ( ImGui::BeginCombo( "Shader", preview.c_str() ) )
        {
            for ( const auto& name : shaderService->GetAllNames() )
            {
                auto candidate = shaderService->GetByName( name );
                if ( !candidate ||
                     candidate->GetProgramMeta().Domain != ::Desert::Core::Formats::ShaderDomain::Terrain )
                    continue;
                const bool selected = ( name == mat.ShaderName );
                if ( ImGui::Selectable( name.c_str(), selected ) && !selected )
                {
                    mat.ShaderName = name;
                    mat.Params.clear();
                }
                if ( selected )
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if ( mat.ShaderName.empty() )
        {
            ImGui::TextDisabled( "Pick a terrain shader above." );
            return;
        }

        auto shader = shaderService->GetByName( mat.ShaderName );
        if ( !shader )
            return;
        const auto& schema = shader->GetProgramMeta();
        if ( schema.Params.empty() )
            return;

        if ( !ImGui::BeginTable( "##terrain_mat", 2,
                                 ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings ) )
            return;
        ImGui::TableSetupColumn( "label", ImGuiTableColumnFlags_WidthStretch, 0.38f );
        ImGui::TableSetupColumn( "control", ImGuiTableColumnFlags_WidthStretch, 0.62f );

        const auto findOrAdd =
             [&]( const ::Desert::Core::Formats::ShaderParam& p ) -> ::Desert::ECS::MaterialParamOverride&
        {
            for ( auto& o : mat.Params )
                if ( o.Name == p.Name )
                    return o;
            mat.Params.push_back( { p.Name, p.Default } );
            return mat.Params.back();
        };

        for ( const auto& p : schema.Params )
        {
            using W  = ::Desert::Core::Formats::ShaderParamWidget;
            using VT = ::Desert::Core::Formats::ShaderValueType;

            const char*       label    = p.DisplayName.empty() ? p.Name.c_str() : p.DisplayName.c_str();
            const std::string hiddenId = "##tp_" + p.Name;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( label );
            ImGui::TableNextColumn();
            ImGui::PushItemWidth( -FLT_MIN );

            if ( p.IsTexture )
            {
                ::Desert::ECS::MaterialTextureOverride* texOv = nullptr;
                for ( auto& t : mat.Textures )
                    if ( t.Name == p.Name )
                    {
                        texOv = &t;
                        break;
                    }
                if ( !texOv )
                {
                    mat.Textures.push_back( { p.Name, 0 } );
                    texOv = &mat.Textures.back();
                }

                std::string disp = "<drop texture>";
                if ( texOv->TextureHandle != 0 && assetMgr )
                {
                    if ( auto tex = assetMgr->FindByHandle<::Desert::Assets::TextureAsset>(
                              ::Common::UUID( texOv->TextureHandle ) ) )
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
                        if ( assetMgr )
                        {
                            const auto resolved = ::Desert::Editor::TextureDnD::ResolveOrImport( *assetMgr, path );
                            if ( static_cast<uint64_t>( resolved ) != 0 )
                                texOv->TextureHandle = static_cast<uint64_t>( resolved );
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopItemWidth();
                continue;
            }

            auto& ov = findOrAdd( p );
            if ( p.Widget == W::Color )
            {
                if ( p.Type == VT::Float3 )
                    ImGui::ColorEdit3( hiddenId.c_str(), &ov.Value.x );
                else
                    ImGui::ColorEdit4( hiddenId.c_str(), &ov.Value.x );
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
                    ImGui::SliderScalarN( hiddenId.c_str(), ImGuiDataType_Float, &ov.Value.x, comps, &mn, &mx );
                }
                else
                {
                    ImGui::DragScalarN( hiddenId.c_str(), ImGuiDataType_Float, &ov.Value.x, comps, 0.01f );
                }
            }
            ImGui::PopItemWidth();
        }
        ImGui::EndTable();
    }

    // Sizes a collider to the entity's mesh bounds (so the green wireframe wraps the visible object —
    // UE auto-fits collision to the mesh instead of leaving a default 0.5 cube). HalfExtents/Radius are
    // world units, so we multiply the local AABB by the entity's scale (PhysicsECSSystem feeds these to
    // Jolt directly, ignoring the transform's scale).
    static void FitColliderToMesh( ::Desert::ECS::Entity& entity, ::Desert::ECS::ColliderData& col )
    {
        if ( !entity.HasComponent<::Desert::ECS::StaticMeshComponent>() )
            return;

        const auto&     smc  = entity.GetComponent<::Desert::ECS::StaticMeshComponent>();
        ::Desert::Mesh* mesh = nullptr;
        if ( smc.MeshHandle )
            mesh = ::Desert::Runtime::ResourceRegistry::GetMeshService()->Get( smc.MeshHandle );
        else if ( smc.RuntimeMesh )
            mesh = smc.RuntimeMesh.get();
        else if ( smc.Primitive.has_value() )
            mesh = ::Desert::Geometry::PrimitiveMeshFactory::GetShared( smc.Primitive.value() );
        if ( !mesh )
            return;

        glm::vec3 mn( std::numeric_limits<float>::max() );
        glm::vec3 mx( std::numeric_limits<float>::lowest() );
        for ( const auto& sm : mesh->GetSubmeshes() )
        {
            mn = glm::min( mn, sm.BoundingBox.Min );
            mx = glm::max( mx, sm.BoundingBox.Max );
        }
        if ( mn.x > mx.x )
            return; // no submeshes / empty mesh

        const glm::vec3 scale = entity.GetComponent<::Desert::ECS::TransformComponent>().Scale;
        const glm::vec3 half  = glm::abs( ( mx - mn ) * 0.5f * scale );

        col.HalfExtents = half;
        col.Radius      = glm::max( half.x, glm::max( half.y, half.z ) );
        // Capsule cylinder half-height = total half-height minus the two hemispherical caps (radius).
        col.HalfHeight = glm::max( 0.01f, half.y - col.Radius );
    }

    // Collider editor: same auto-built reflected UI as the one-liner, PLUS a one-time auto-fit on Add and
    // a manual "Fit to Mesh Bounds" button.
    // Terrain: reflected TerrainData UI + the terrain MATERIAL (shader + schema params) in ONE
    // section. Terrain has no mesh slots, so its material lives on a MaterialComponent that this
    // entry manages implicitly — no separate component for the user to discover or confuse.
    static ComponentEditorEntry MakeTerrainEntry()
    {
        ComponentEditorEntry e;
        e.Name      = "Terrain";
        e.CanRemove = true;
        e.Has    = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<::Desert::ECS::TerrainComponent>(); };
        e.Add    = []( ::Desert::ECS::Entity& en ) { en.AddComponent<::Desert::ECS::TerrainComponent>(); };
        e.Remove = []( ::Desert::ECS::Entity& en )
        {
            en.RemoveComponent<::Desert::ECS::TerrainComponent>();
            // The terrain's material rides along (it has no meaning without the terrain).
            if ( en.HasComponent<::Desert::ECS::MaterialComponent>() )
                en.RemoveComponent<::Desert::ECS::MaterialComponent>();
        };
        e.Draw = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& ctx )
        {
            auto& c = en.GetComponent<::Desert::ECS::TerrainComponent>();
            PropertyEditorBuilder::Draw( &c.Data, "TerrainData", ctx.AssetMgr(), ctx.UIHelper );

            ::ImGui::Separator();
            ::ImGui::TextDisabled( "Material" );
            if ( !en.HasComponent<::Desert::ECS::MaterialComponent>() )
                en.AddComponent<::Desert::ECS::MaterialComponent>();
            DrawTerrainMaterialWidget( en, ctx.AssetMgr() );
        };
        return e;
    }

    static ComponentEditorEntry MakeColliderEntry()
    {
        ComponentEditorEntry e;
        e.Name      = "Collider";
        e.CanRemove = true;
        e.Has = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<::Desert::ECS::ColliderComponent>(); };
        e.Add = []( ::Desert::ECS::Entity& en )
        {
            auto& c = en.AddComponent<::Desert::ECS::ColliderComponent>();
            FitColliderToMesh( en, c.Data );
        };
        e.Remove = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<::Desert::ECS::ColliderComponent>(); };
        e.Draw   = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& ctx )
        {
            auto& c = en.GetComponent<::Desert::ECS::ColliderComponent>();
            PropertyEditorBuilder::Draw( &c.Data, "ColliderData", ctx.AssetMgr(), ctx.UIHelper );
            if ( ::ImGui::Button( "Fit to Mesh Bounds", ImVec2( -1.0f, 0.0f ) ) )
                FitColliderToMesh( en, c.Data );
        };
        return e;
    }

    // UI Layout (RectTransform): anchor-preset controls ("Fill / Match Parent" + 4x4 grid) on top of the
    // reflected anchor/offset/pivot fields, so you can match the parent from the inspector (not just the
    // viewport toolbar). Presets act in design space (keep the authored size; stretch fills the axis).
    static ComponentEditorEntry MakeUILayoutEntry()
    {
        using C = ::Desert::ECS::UILayoutComponent;
        ComponentEditorEntry e;
        e.Name      = "UI Layout";
        e.CanRemove = true;
        e.Has       = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<C>(); };
        e.Add       = []( ::Desert::ECS::Entity& en ) { en.AddComponent<C>(); };
        e.Remove    = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<C>(); };
        e.Draw      = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& ctx )
        {
            auto& c = en.GetComponent<C>();
            UIAnchors::DrawControls( c.Data );
            PropertyEditorBuilder::Draw( &c.Data, "UILayoutData", ctx.AssetMgr(), ctx.UIHelper );
        };
        return e;
    }
    // UE-style Instanced Static Mesh editor: pick a primitive (or drop an asset mesh) + add/clear instances.
    // Instances are WORLD-space; all of them render as ONE instanced draw (+1 per shadow cascade).
    static ComponentEditorEntry MakeInstancedStaticMeshEntry()
    {
        using ISMC = ::Desert::ECS::InstancedStaticMeshComponent;
        ComponentEditorEntry e;
        e.Name      = "Instanced Static Mesh";
        e.CanRemove = true;
        e.Has       = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<ISMC>(); };
        e.Add       = []( ::Desert::ECS::Entity& en )
        {
            auto& c     = en.AddComponent<ISMC>();
            c.Primitive = ::Desert::Geometry::PrimitiveType::Cube; // renders immediately
            if ( c.InstanceTransforms.empty() )
                c.InstanceTransforms.push_back( glm::mat4( 1.0f ) );
        };
        e.Remove = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<ISMC>(); };
        e.Draw   = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& ctx )
        {
            auto& c = en.GetComponent<ISMC>();

            // Mesh source: a built-in primitive, or an asset mesh dropped from the browser.
            static const char* kPrims[] = { "Cube", "Sphere", "Plane", "Pyramid" };
            int                cur      = c.Primitive.has_value() ? static_cast<int>( c.Primitive.value() ) : 0;
            if ( !c.MeshHandle && ::ImGui::Combo( "Primitive", &cur, kPrims, IM_ARRAYSIZE( kPrims ) ) )
            {
                c.Primitive = static_cast<::Desert::Geometry::PrimitiveType>( cur );
                c.RuntimeMesh.reset();
            }

            ::ImGui::Button( c.MeshHandle ? "Mesh: <asset> (drop to replace)"
                                          : "Drop a .stmesh to use an asset mesh",
                             ImVec2( -1.0f, 0.0f ) );
            if ( ::ImGui::BeginDragDropTarget() )
            {
                if ( const ImGuiPayload* pl =
                          ::ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::MeshAsset );
                     pl && ctx.AssetMgr() )
                {
                    const std::string path( static_cast<const char*>( pl->Data ),
                                            pl->DataSize > 0 ? pl->DataSize - 1 : 0 );
                    const auto        handle = ::Desert::Editor::MeshDnD::ResolveOrImport( *ctx.AssetMgr(), path );
                    if ( !handle.IsNull() )
                    {
                        c.MeshHandle = handle;
                        c.Primitive.reset();
                        c.RuntimeMesh.reset();
                    }
                }
                ::ImGui::EndDragDropTarget();
            }
            if ( c.MeshHandle && ::ImGui::SmallButton( "Use Primitive Instead" ) )
            {
                c.MeshHandle = {};
                c.Primitive  = ::Desert::Geometry::PrimitiveType::Cube;
            }

            ::ImGui::Separator();
            ::ImGui::Text( "Instances: %d", static_cast<int>( c.InstanceTransforms.size() ) );

            if ( ::ImGui::Button( "Add Instance" ) )
            {
                const float n = static_cast<float>( c.InstanceTransforms.size() );
                c.InstanceTransforms.push_back( glm::translate( glm::mat4( 1.0f ), glm::vec3( n * 2.0f, 0, 0 ) ) );
                c.InstancesDirty = true;
            }
            ::ImGui::SameLine();
            if ( ::ImGui::Button( "Add 10x10 Grid" ) )
            {
                for ( int z = 0; z < 10; ++z )
                    for ( int x = 0; x < 10; ++x )
                        c.InstanceTransforms.push_back(
                             glm::translate( glm::mat4( 1.0f ), glm::vec3( x * 2.0f, 0.0f, z * 2.0f ) ) );
                c.InstancesDirty = true;
            }
            ::ImGui::SameLine();
            if ( ::ImGui::Button( "Clear" ) )
            {
                c.InstanceTransforms.clear();
                c.InstancesDirty = true;
            }
        };
        return e;
    }

    // Blendshape / morph-target editor: one slider per morph target of the entity's mesh (static or skinned).
    // Names + count come from the mesh asset; the sliders write MorphComponent::Weights (index-aligned).
    static ComponentEditorEntry MakeMorphEntry()
    {
        using MC = ::Desert::ECS::MorphComponent;
        ComponentEditorEntry e;
        e.Name      = "Morph Targets";
        e.CanRemove = true;
        e.Has       = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<MC>(); };
        e.Add       = []( ::Desert::ECS::Entity& en ) { en.AddComponent<MC>(); };
        e.Remove    = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<MC>(); };
        e.Draw      = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& )
        {
            namespace ImGui = ::ImGui;
            auto& mc        = en.GetComponent<MC>();

            ::Desert::Assets::AssetHandle meshHandle;
            if ( en.HasComponent<::Desert::ECS::SkinnedMeshComponent>() )
                meshHandle = en.GetComponent<::Desert::ECS::SkinnedMeshComponent>().MeshHandle;
            else if ( en.HasComponent<::Desert::ECS::StaticMeshComponent>() )
                meshHandle = en.GetComponent<::Desert::ECS::StaticMeshComponent>().MeshHandle;

            const ::Desert::Assets::MeshAsset* meshAsset =
                 meshHandle ? ::Desert::Runtime::ResourceRegistry::GetMeshService()->GetAsset( meshHandle )
                            : nullptr;

            if ( !meshAsset || meshAsset->GetMorphTargets().empty() )
            {
                ImGui::TextDisabled( "This entity's mesh has no blendshapes (morph targets)." );
                return;
            }

            const auto& targets = meshAsset->GetMorphTargets();

            // Keep the component's arrays in step with the mesh's targets (the mesh may have been swapped).
            if ( mc.Weights.size() != targets.size() )
                mc.Weights.resize( targets.size(), 0.0f );
            mc.TargetNames.resize( targets.size() );
            for ( size_t i = 0; i < targets.size(); ++i )
                mc.TargetNames[i] = targets[i].Name;

            ImGui::TextDisabled( "%d blendshape(s)", static_cast<int>( targets.size() ) );
            for ( size_t i = 0; i < targets.size(); ++i )
            {
                ImGui::PushID( static_cast<int>( i ) );
                const char* name = mc.TargetNames[i].empty() ? "<unnamed>" : mc.TargetNames[i].c_str();
                ImGui::SliderFloat( name, &mc.Weights[i], 0.0f, 1.0f );
                ImGui::PopID();
            }
            if ( ImGui::Button( "Reset All", ImVec2( -1.0f, 0.0f ) ) )
                std::fill( mc.Weights.begin(), mc.Weights.end(), 0.0f );
        };
        return e;
    }
} // namespace Desert::Editor

namespace
{
    const int _desert_collider_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeColliderEntry() );
    const int _desert_terrain_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeTerrainEntry() );

    const int _desert_ism_component_reg = ::Desert::Editor::ComponentWidgetRegistry::Get().Register(
         ::Desert::Editor::MakeInstancedStaticMeshEntry() );

    const int _desert_morph_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeMorphEntry() );

    const int _desert_uilayout_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeUILayoutEntry() );
} // namespace

// SINGLE SOURCE OF TRUTH: for MESH entities, materials (shader + params) are authored ONLY in
// the material slots (PBR Materials -> Shader picker inside each material). The old standalone
// "Shader Override" editor is gone; MaterialComponent remains (a) the RUNTIME override channel
// for scripts / legacy scenes — surfaced by the PBR Materials banner with one-click clear —
// and (b) the TERRAIN material holder (terrain has no mesh slots), edited below.
// Script component: an entity can run SEVERAL scripts (like UE ActorComponents), shown as a list of slots.
// Per slot: pick the .lua from a dropdown OR drag one from the File Explorer, Reload (hot-reload), and edit
// the script's exposed Properties. "+ Add Script" appends a slot; the X removes one.
DESERT_REGISTER_CUSTOM_COMPONENT(
     ::Desert::ECS::ScriptComponent, "Script", true,
     (
          []( ::Desert::ECS::Entity& e, ::Desert::Core::Scene*, const ::Desert::Editor::ComponentEditContext& )
          {
              namespace fs = std::filesystem;
              auto& sc     = e.GetComponent<::Desert::ECS::ScriptComponent>();

              int removeIndex = -1;
              for ( size_t i = 0; i < sc.Scripts.size(); ++i )
              {
                  ImGui::PushID( static_cast<int>( i ) );
                  auto& slot = sc.Scripts[i];

                  const std::string preview = slot.ScriptPath.empty()
                                                   ? "Select script..."
                                                   : fs::path( slot.ScriptPath ).filename().string();

                  ImGui::SetNextItemWidth( -60.0f ); // leave room for the remove button
                  if ( ImGui::BeginCombo( "##ScriptSel", preview.c_str() ) )
                  {
                      std::error_code ec;
                      if ( fs::exists( "Resources", ec ) )
                      {
                          for ( const auto& it : fs::recursive_directory_iterator( "Resources", ec ) )
                          {
                              if ( !it.is_regular_file() || it.path().extension() != ".lua" )
                                  continue;
                              const std::string rel = it.path().generic_string();
                              if ( ImGui::Selectable( it.path().filename().string().c_str(),
                                                      slot.ScriptPath == rel ) )
                              {
                                  slot.ScriptPath = rel;
                                  slot.Started    = false;
                                  slot.Properties.clear(); // re-seed from the new script's schema below
                              }
                          }
                      }
                      ImGui::EndCombo();
                  }

                  // Drag a .lua from the File Explorer onto the combo.
                  if ( ImGui::BeginDragDropTarget() )
                  {
                      if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "AssetFile" ) )
                      {
                          const std::string dropped( static_cast<const char*>( payload->Data ) );
                          if ( dropped.size() > 4 && dropped.substr( dropped.size() - 4 ) == ".lua" )
                          {
                              slot.ScriptPath = fs::path( dropped ).generic_string();
                              slot.Started    = false;
                              slot.Properties.clear();
                          }
                      }
                      ImGui::EndDragDropTarget();
                  }

                  ImGui::SameLine();
                  if ( ImGui::Button( "X" ) )
                      removeIndex = static_cast<int>( i );

                  if ( !slot.ScriptPath.empty() )
                  {
                      ImGui::TextDisabled( "%s", slot.ScriptPath.c_str() );

                      if ( ImGui::Button( "Reload" ) )
                          slot.Started = false; // re-read on the next Play frame (hot-reload)

                      // ---- Exposed properties (the script's `Properties` table) ----
                      if ( slot.Properties.empty() )
                          slot.Properties = ::Desert::Scripting::ReadScriptProperties( slot.ScriptPath );

                      ImGui::SameLine();
                      if ( ImGui::Button( "Refresh Props" ) )
                      {
                          // Re-read the schema, keeping existing values for properties that still exist.
                          auto schema = ::Desert::Scripting::ReadScriptProperties( slot.ScriptPath );
                          for ( auto& s : schema )
                          {
                              auto old = std::find_if( slot.Properties.begin(), slot.Properties.end(),
                                                       [&]( const auto& p )
                                                       { return p.Name == s.Name && p.Type == s.Type; } );
                              if ( old != slot.Properties.end() )
                                  s = *old;
                          }
                          slot.Properties = std::move( schema );
                      }

                      for ( auto& p : slot.Properties )
                      {
                          switch ( p.Type )
                          {
                              case ::Desert::Scripting::PropertyType::Number:
                              {
                                  float v = static_cast<float>( p.Number );
                                  if ( ImGui::DragFloat( p.Name.c_str(), &v, 0.01f ) )
                                      p.Number = v;
                                  break;
                              }
                              case ::Desert::Scripting::PropertyType::Bool:
                                  ImGui::Checkbox( p.Name.c_str(), &p.Bool );
                                  break;
                              case ::Desert::Scripting::PropertyType::String:
                              {
                                  char buf[256] = { 0 };
                                  std::strncpy( buf, p.Str.c_str(), sizeof( buf ) - 1 );
                                  if ( ImGui::InputText( p.Name.c_str(), buf, sizeof( buf ) ) )
                                      p.Str = buf;
                                  break;
                              }
                          }
                      }
                  }

                  ImGui::Separator();
                  ImGui::PopID();
              }

              if ( removeIndex >= 0 )
                  sc.Scripts.erase( sc.Scripts.begin() + removeIndex );

              if ( ImGui::Button( "+ Add Script" ) )
                  sc.Scripts.emplace_back();
          } ) )

// Text (SDF world-space label). Simple field editor; the mesh rebuilds automatically when Text/
// Font/Size change (TextECSSystem compares against its Built* cache).
DESERT_REGISTER_CUSTOM_COMPONENT(
     ::Desert::ECS::TextComponent, "Text", true,
     (
          []( ::Desert::ECS::Entity& e, ::Desert::Core::Scene*, const ::Desert::Editor::ComponentEditContext& )
          {
              auto& tc = e.GetComponent<::Desert::ECS::TextComponent>();

              char buf[512] = { 0 };
              std::strncpy( buf, tc.Text.c_str(), sizeof( buf ) - 1 );
              if ( ImGui::InputTextMultiline( "Text", buf, sizeof( buf ), ImVec2( 0, 60 ) ) )
                  tc.Text = buf;

              // Font: an ASSET HANDLE (never a raw path) — pick one of the preloaded fonts from the dropdown
              // or drag a .ttf from the Content Browser. FontService owns the handle<->path registry and the
              // preloaded set; "Default" (null handle) falls back to the engine's built-in font.
              auto*             fs      = ::Desert::Runtime::ResourceRegistry::GetFontService();
              const uint64_t    curHnd  = static_cast<uint64_t>( tc.Font );
              const std::string curPath = fs ? fs->PathForHandle( curHnd ) : "";
              const std::string preview =
                   curHnd == 0
                        ? "Default"
                        : ( curPath.empty() ? "(missing)" : std::filesystem::path( curPath ).stem().string() );
              if ( ImGui::BeginCombo( "Font", preview.c_str() ) )
              {
                  if ( ImGui::Selectable( "Default", curHnd == 0 ) )
                      tc.Font = ::Desert::Assets::AssetHandle();
                  if ( fs )
                  {
                      for ( const auto& f : fs->AvailableFonts() )
                      {
                          const uint64_t h   = fs->RegisterFont( f );
                          const bool     sel = ( h == curHnd );
                          if ( ImGui::Selectable( std::filesystem::path( f ).stem().string().c_str(), sel ) )
                              tc.Font = ::Desert::Assets::AssetHandle( h );
                          if ( sel )
                              ImGui::SetItemDefaultFocus();
                      }
                  }
                  ImGui::EndCombo();
              }
              if ( ImGui::BeginDragDropTarget() )
              {
                  if ( const ImGuiPayload* pl =
                            ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::FontFile ) )
                  {
                      const std::string path( static_cast<const char*>( pl->Data ),
                                              pl->DataSize > 0 ? pl->DataSize - 1 : 0 );
                      if ( fs && !path.empty() )
                          tc.Font = ::Desert::Assets::AssetHandle( fs->RegisterFont( path ) );
                  }
                  ImGui::EndDragDropTarget();
              }
              if ( ImGui::IsItemHovered() )
                  ImGui::SetTooltip( "Pick a preloaded font or drag a .ttf here from the Content Browser" );

              ImGui::ColorEdit4( "Color", &tc.Color.x );
              ImGui::DragFloat( "Size", &tc.Size, 1.0f, 1.0f, 10000.0f, "%.1f cm" );
              ImGui::DragFloat( "Emissive Intensity", &tc.EmissiveIntensity, 0.05f, 0.0f, 20.0f, "%.2f" );
              if ( ImGui::IsItemHovered() )
                  ImGui::SetTooltip( "> ~1 makes the text bloom (it renders into the HDR scene)" );
              ImGui::Checkbox( "Billboard", &tc.Billboard );
          } ) )

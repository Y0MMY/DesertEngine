// Reflected component editors: the full Details UI is auto-built from each data block's REFLECT()
// metadata (PropertyEditorBuilder) — no widget class, no edit to ComponentEditor. To expose a new
// reflected component in the editor, copy one line below.

#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>

#include <Engine/ECS/Components.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
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
#include <Common/Utilities/FileSystem.hpp>

#include <filesystem>
#include <limits>

DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::DirectionLightComponent, Data, "DirectionalLightData",
                                     "Directional Light" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::PointLightComponent, Data, "PointLightData", "Point Light" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::SpotLightComponent, Data, "SpotLightData", "Spot Light" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::CameraComponent, Data, "CameraData", "Camera" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::TerrainComponent, Data, "TerrainData", "Terrain" )
// Collider is registered as a CUSTOM component below (auto-fit to mesh bounds on add) instead of the
// plain reflected one-liner — see MakeColliderEntry.
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::RigidBodyComponent, Data, "RigidBodyData", "Rigid Body" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::CharacterControllerComponent, Data,
                                     "CharacterControllerData", "Character Controller" )

namespace Desert::Editor
{
    // Fully data-driven material editor: a shader picker + controls auto-built from the selected shader's
    // #pragma param schema (no per-shader UI code). Overrides are stored by name in the MaterialComponent.
    static void DrawMaterialComponentWidget( ::Desert::ECS::Entity& entity, ::Desert::Assets::AssetManager* assetMgr )
    {
        auto& mat = entity.GetComponent<::Desert::ECS::MaterialComponent>();

        // A material only does anything on a renderable. Warn (don't crash/hide) on non-renderables.
        const bool hasRenderable = entity.HasComponent<::Desert::ECS::StaticMeshComponent>() ||
                                   entity.HasComponent<::Desert::ECS::SkinnedMeshComponent>() ||
                                   entity.HasComponent<::Desert::ECS::TerrainComponent>();
        if ( !hasRenderable )
        {
            ::ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.75f, 0.2f, 1.0f ) );
            ::ImGui::TextWrapped( "%s Material has no effect: this entity has no renderable. Add a Mesh or "
                                  "Terrain.",
                                  ICON_MDI_ALERT );
            ::ImGui::PopStyleColor();
        }

        auto* shaderService = ::Desert::Runtime::ResourceRegistry::GetShaderService();
        if ( !shaderService )
            return;

        // --- Shader picker, filtered by the renderable's domain (mesh -> surface, terrain -> terrain).
        // PBR shaders are NOT listed (PBR is specialized + uses the mesh's material slots, not this).
        using Domain         = ::Desert::Core::Formats::ShaderDomain;
        const Domain wanted  = entity.HasComponent<::Desert::ECS::TerrainComponent>() ? Domain::Terrain
                                                                                      : Domain::Surface;
        const std::string preview = mat.ShaderName.empty() ? "<none>" : mat.ShaderName;
        if ( ::ImGui::BeginCombo( "Shader", preview.c_str() ) )
        {
            for ( const auto& name : shaderService->GetAllNames() )
            {
                auto candidate = shaderService->GetByName( name );
                if ( !candidate || candidate->GetProgramMeta().Domain != wanted )
                    continue;

                const bool selected = ( name == mat.ShaderName );
                if ( ::ImGui::Selectable( name.c_str(), selected ) && name != mat.ShaderName )
                {
                    mat.ShaderName = name;
                    mat.Params.clear(); // rebuilt from the new shader's schema below
                }
                if ( selected )
                    ::ImGui::SetItemDefaultFocus();
            }
            ::ImGui::EndCombo();
        }

        if ( mat.ShaderName.empty() )
        {
            ::ImGui::TextDisabled( "No shader selected - pick one above, or remove this component." );
            return;
        }

        auto shader = shaderService->GetByName( mat.ShaderName );
        if ( !shader )
        {
            ::ImGui::TextDisabled( "Shader '%s' not found", mat.ShaderName.c_str() );
            return;
        }

        const auto& schema = shader->GetProgramMeta();
        if ( schema.Params.empty() )
        {
            ::ImGui::TextDisabled( "Shader exposes no #pragma param" );
            return;
        }

        // Ensure an override entry exists for each schema param (seeded with its #pragma default).
        auto findOrAdd = [&]( const ::Desert::Core::Formats::ShaderParam& p ) -> ::Desert::ECS::MaterialParamOverride&
        {
            for ( auto& o : mat.Params )
                if ( o.Name == p.Name )
                    return o;
            mat.Params.push_back( { p.Name, p.Default } );
            return mat.Params.back();
        };

        ::ImGui::Separator();
        for ( const auto& p : schema.Params )
        {
            using W  = ::Desert::Core::Formats::ShaderParamWidget;
            using VT = ::Desert::Core::Formats::ShaderValueType;

            const char* label = p.DisplayName.empty() ? p.Name.c_str() : p.DisplayName.c_str();

            if ( p.IsTexture )
            {
                // Find/create the texture override entry for this sampler.
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
                        const auto& src = tex->GetSourcePath();
                        const auto  path = !src.empty() ? src : tex->GetMetadata().Filepath.string();
                        disp             = std::filesystem::path( path ).filename().string();
                    }
                }

                ::ImGui::TextUnformatted( label );
                ::ImGui::SameLine();
                ::ImGui::Button( ( disp + "##tex_" + p.Name ).c_str(), ImVec2( -1.0f, 0.0f ) );
                if ( ::ImGui::BeginDragDropTarget() )
                {
                    if ( const ImGuiPayload* pl = ::ImGui::AcceptDragDropPayload( "TEXTURE_ASSET" ) )
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
                    ::ImGui::EndDragDropTarget();
                }
                continue;
            }

            auto& ov = findOrAdd( p );

            if ( p.Widget == W::Color )
            {
                if ( p.Type == VT::Float3 )
                    ::ImGui::ColorEdit3( label, &ov.Value.x );
                else
                    ::ImGui::ColorEdit4( label, &ov.Value.x );
                continue;
            }

            int comps = ( p.Type == VT::Float2 ) ? 2 : ( p.Type == VT::Float3 ) ? 3 : ( p.Type == VT::Float4 ) ? 4 : 1;
            if ( p.Min.has_value() && p.Max.has_value() )
            {
                float mn = *p.Min, mx = *p.Max;
                ::ImGui::SliderScalarN( label, ImGuiDataType_Float, &ov.Value.x, comps, &mn, &mx );
            }
            else
            {
                ::ImGui::DragScalarN( label, ImGuiDataType_Float, &ov.Value.x, comps, 0.01f );
            }
        }

        // --- Save / Load this material to a reusable .demat file (MVP; full asset integration later) ---
        ::ImGui::Separator();
        static std::string s_matPath = "Resources/Assets/Material/MyMaterial.demat";
        Utils::ImGuiUtilities::InputText( s_matPath, "##matpath" );
        if ( ::ImGui::Button( "Save Material" ) && assetMgr )
        {
            const std::string js =
                 ::Desert::Core::Serialize::SaveMaterialComponentToJson( mat, *assetMgr );
            ::Common::Utils::FileSystem::WriteContentToFile( s_matPath, js );
        }
        ::ImGui::SameLine();
        if ( ::ImGui::Button( "Load (from path)" ) && assetMgr )
        {
            const std::string js = ::Common::Utils::FileSystem::ReadFileContent( s_matPath );
            if ( !js.empty() )
                ::Desert::Core::Serialize::LoadMaterialComponentFromJson( js, mat, *assetMgr );
        }

        // The obvious way to load: drag a .demat from the File Explorer onto this target.
        ::ImGui::Button( "  Drag a .demat here to load  ", ImVec2( -1.0f, 0.0f ) );
        if ( ::ImGui::BeginDragDropTarget() )
        {
            if ( const ImGuiPayload* pl = ::ImGui::AcceptDragDropPayload( "MATERIAL_ASSET" ); pl && assetMgr )
            {
                const std::string path( static_cast<const char*>( pl->Data ),
                                        pl->DataSize > 0 ? pl->DataSize - 1 : 0 );
                const std::string js = ::Common::Utils::FileSystem::ReadFileContent( path );
                // Only generic materials parse (PBR .demat lacks ShaderName -> load returns false, ignored).
                if ( !js.empty() )
                    ::Desert::Core::Serialize::LoadMaterialComponentFromJson( js, mat, *assetMgr );
            }
            ::ImGui::EndDragDropTarget();
        }
    }

    // Sizes a collider to the entity's mesh bounds (so the green wireframe wraps the visible object —
    // UE auto-fits collision to the mesh instead of leaving a default 0.5 cube). HalfExtents/Radius are
    // world units, so we multiply the local AABB by the entity's scale (PhysicsECSSystem feeds these to
    // Jolt directly, ignoring the transform's scale).
    static void FitColliderToMesh( ::Desert::ECS::Entity& entity, ::Desert::ECS::ColliderData& col )
    {
        if ( !entity.HasComponent<::Desert::ECS::StaticMeshComponent>() )
            return;

        const auto&    smc  = entity.GetComponent<::Desert::ECS::StaticMeshComponent>();
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
        col.HalfHeight  = glm::max( 0.01f, half.y - col.Radius );
    }

    // Collider editor: same auto-built reflected UI as the one-liner, PLUS a one-time auto-fit on Add and
    // a manual "Fit to Mesh Bounds" button.
    static ComponentEditorEntry MakeColliderEntry()
    {
        ComponentEditorEntry e;
        e.Name      = "Collider";
        e.CanRemove = true;
        e.Has       = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<::Desert::ECS::ColliderComponent>(); };
        e.Add       = []( ::Desert::ECS::Entity& en )
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
                if ( const ImGuiPayload* pl = ::ImGui::AcceptDragDropPayload( "MESH_ASSET" );
                     pl && ctx.AssetMgr() )
                {
                    const std::string path( static_cast<const char*>( pl->Data ),
                                            pl->DataSize > 0 ? pl->DataSize - 1 : 0 );
                    const auto handle = ::Desert::Editor::MeshDnD::ResolveOrImport( *ctx.AssetMgr(), path );
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
} // namespace Desert::Editor

namespace
{
    const int _desert_collider_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeColliderEntry() );

    const int _desert_ism_component_reg = ::Desert::Editor::ComponentWidgetRegistry::Get().Register(
         ::Desert::Editor::MakeInstancedStaticMeshEntry() );
}

DESERT_REGISTER_CUSTOM_COMPONENT(
     ::Desert::ECS::MaterialComponent, "Material", true,
     ( []( ::Desert::ECS::Entity& e, ::Desert::Core::Scene*, const ::Desert::Editor::ComponentEditContext& ctx )
       { ::Desert::Editor::DrawMaterialComponentWidget( e, ctx.AssetMgr() ); } ) )

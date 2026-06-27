// Reflected component editors: the full Details UI is auto-built from each data block's REFLECT()
// metadata (PropertyEditorBuilder) — no widget class, no edit to ComponentEditor. To expose a new
// reflected component in the editor, copy one line below.

#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>

#include <Engine/ECS/Components.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Graphic/Shader.hpp>
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

DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::DirectionLightComponent, Data, "DirectionalLightData",
                                     "Directional Light" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::PointLightComponent, Data, "PointLightData", "Point Light" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::SpotLightComponent, Data, "SpotLightData", "Spot Light" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::CameraComponent, Data, "CameraData", "Camera" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::TerrainComponent, Data, "TerrainData", "Terrain" )

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
} // namespace Desert::Editor

DESERT_REGISTER_CUSTOM_COMPONENT(
     ::Desert::ECS::MaterialComponent, "Material", true,
     ( []( ::Desert::ECS::Entity& e, ::Desert::Core::Scene*, const ::Desert::Editor::ComponentEditContext& ctx )
       { ::Desert::Editor::DrawMaterialComponentWidget( e, ctx.AssetMgr() ); } ) )

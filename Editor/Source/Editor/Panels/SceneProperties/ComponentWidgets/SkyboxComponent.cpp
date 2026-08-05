#include "SkyboxComponent.hpp"
#include <Editor/Core/DragPayloads.hpp>

#include <ImGui/imgui.h>

#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>
#include <Editor/Panels/PropertyEditor/PropertyEditorBuilder.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Skybox/SkyboxAsset.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Core/Scene.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <cmath>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
        ImU32 ToU32( const glm::vec3& c )
        {
            return ImGui::ColorConvertFloat4ToU32( ImVec4( c.r, c.g, c.b, 1.0f ) );
        }

        // A ramp of the sky's OWN authored colours (zenith -> horizon -> ground) with the sun marked at
        // its current elevation. Deliberately NOT a render of the sky shader: it is a legend for the
        // colour fields below, readable while you drag them, and it costs nothing.
        void DrawSkyRamp( const ECS::SkyboxComponent& sky, float sunElevationDeg, bool haveSun )
        {
            const float  width  = ImGui::GetContentRegionAvail().x;
            const float  height = 96.0f;
            const ImVec2 p0     = ImGui::GetCursorScreenPos();
            const ImVec2 p1( p0.x + width, p0.y + height );
            ImDrawList*  dl = ImGui::GetWindowDrawList();

            // Horizon sits 2/3 down, like the sky itself: most of the frame is sky.
            const float horizonY = p0.y + height * 0.66f;

            // Low sun washes the horizon band with the sunset tint — the same idea the sky shader uses,
            // so the strip tracks the setting you are actually editing.
            const float     sunset  = haveSun ? glm::clamp( 1.0f - sunElevationDeg / 20.0f, 0.0f, 1.0f ) : 0.0f;
            const glm::vec3 horizon = glm::mix( sky.HorizonColor, sky.SunsetColor, sunset * 0.8f );

            dl->AddRectFilledMultiColor( p0, ImVec2( p1.x, horizonY ), ToU32( sky.ZenithColor ),
                                         ToU32( sky.ZenithColor ), ToU32( horizon ), ToU32( horizon ) );
            dl->AddRectFilled( ImVec2( p0.x, horizonY ), p1, ToU32( sky.GroundColor ) );
            dl->AddLine( ImVec2( p0.x, horizonY ), ImVec2( p1.x, horizonY ), IM_COL32( 0, 0, 0, 90 ) );
            dl->AddRect( p0, p1, ImGui::GetColorU32( ImGuiCol_Border ) );

            if ( haveSun )
            {
                // Elevation 90 = top of the strip, 0 = the horizon line, below that = under the ground.
                const float  t = glm::clamp( sunElevationDeg / 90.0f, -1.0f, 1.0f );
                const float  y = t >= 0.0f ? glm::mix( horizonY, p0.y, t ) : glm::mix( horizonY, p1.y, -t );
                const ImVec2 c( p0.x + width * 0.5f, y );
                if ( sunElevationDeg >= 0.0f )
                {
                    dl->AddCircleFilled( c, 9.0f, ToU32( sky.SunColor ), 20 );
                    dl->AddCircle( c, 9.0f, IM_COL32( 0, 0, 0, 60 ), 20 );
                }
                else
                {
                    dl->AddCircle( c, 9.0f, IM_COL32( 150, 160, 190, 200 ), 20, 2.0f );
                }
            }

            ImGui::Dummy( ImVec2( width, height ) );
            if ( !haveSun )
                ImGui::TextDisabled( "No directional light in the scene — sun position unknown" );
        }
    } // namespace

    // Most of the Skybox component's Details UI is AUTO-GENERATED from its REFLECT()/PROPERTY() metadata
    // (PropertyEditorBuilder). The ONE hand-drawn part is the HDR SkyboxAsset picker (a dropdown of loaded
    // skyboxes + drag-drop), because the generic reflected asset slot is texture-oriented and doesn't resolve
    // SkyboxAssets — so SkyboxHandle is marked Hidden and drawn here. Plus the "Bake Sky IBL" action button.
    DESERT_REGISTER_CUSTOM_COMPONENT(
         ECS::SkyboxComponent, "Skybox", false,
         (
              []( ECS::Entity& entity, ::Desert::Core::Scene* scene, const ComponentEditContext& ctx )
              {
                  auto* assetManager = ctx.AssetMgr();
                  if ( !assetManager )
                      return;
                  auto& skybox = entity.GetComponent<ECS::SkyboxComponent>();

                  auto bindSkybox = [&]( const Assets::AssetHandle& handle )
                  {
                      if ( handle == skybox.SkyboxHandle )
                          return;
                      auto& svc = *Runtime::ResourceRegistry::GetSkyboxService();
                      if ( !svc.Get( handle ) )
                      {
                          if ( auto a = assetManager->FindByHandle<Assets::SkyboxAsset>( handle ) )
                          {
                              Graphic::Renderer::GetInstance().WaitDeviceIdle();
                              svc.Register( a );
                          }
                      }
                      skybox.SkyboxHandle = handle;
                  };

                  // ── Source mode (purpose-built control, like the mesh LOD block): one clear switch between an
                  //    HDR cubemap asset and the engine's procedural atmosphere, driving which sections show. ──
                  ImGui::TextUnformatted( "Source" );
                  int mode = skybox.Procedural ? 1 : 0;
                  ImGui::PushItemWidth( ImGui::GetContentRegionAvail().x );
                  if ( ImGui::RadioButton( "HDR Skybox", mode == 0 ) )
                      skybox.Procedural = false;
                  ImGui::SameLine();
                  if ( ImGui::RadioButton( "Procedural Sky", mode == 1 ) )
                      skybox.Procedural = true;
                  ImGui::PopItemWidth();
                  ImGui::Spacing();

                  if ( !skybox.Procedural )
                  {
                      // --- HDR skybox picker (dropdown of loaded SkyboxAssets) ---
                      const auto  current = assetManager->FindByHandle<Assets::SkyboxAsset>( skybox.SkyboxHandle );
                      std::string currentName =
                           current ? Common::Utils::FileSystem::GetFileName( current->GetMetadata().Filepath )
                                   : "None";
                      ImGui::TextUnformatted( "Skybox (HDR)" );
                      if ( ImGui::Button( currentName.c_str(), ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
                          ImGui::OpenPopup( "skybox_selector" );

                      // --- drag-drop a skybox/texture file from the File Explorer ---
                      if ( ImGui::BeginDragDropTarget() )
                      {
                          const char* types[] = { ::Desert::Editor::DragPayloads::SkyboxAsset,
                                                  ::Desert::Editor::DragPayloads::TextureAsset, "AssetFile" };
                          for ( const char* t : types )
                          {
                              if ( const ImGuiPayload* p = ImGui::AcceptDragDropPayload( t ) )
                              {
                                  const std::string path( static_cast<const char*>( p->Data ) );
                                  if ( auto a = assetManager->FindByPath<Assets::SkyboxAsset>( path ) )
                                      bindSkybox( a->GetMetadata().Handle );
                                  break;
                              }
                          }
                          ImGui::EndDragDropTarget();
                      }

                      if ( ImGui::BeginPopup( "skybox_selector" ) )
                      {
                          static ImGuiTextFilter filter;
                          filter.Draw( "##Search", 200 );
                          ImGui::Separator();
                          auto skyboxes = assetManager->FindAllByType<Assets::SkyboxAsset>();
                          for ( const auto& [handle, asset] : skyboxes )
                          {
                              const std::string name =
                                   Common::Utils::FileSystem::GetFileName( asset->GetMetadata().Filepath );
                              if ( filter.PassFilter( name.c_str() ) &&
                                   ImGui::Selectable( name.c_str(), handle == skybox.SkyboxHandle ) )
                                  bindSkybox( handle );
                          }
                          if ( skyboxes.empty() )
                              ImGui::TextDisabled( "No skybox assets available" );
                          ImGui::EndPopup();
                      }

                      ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x );
                      ImGui::SliderFloat( "Intensity", &skybox.Intensity, 0.0f, 10.0f );
                  }

                  // --- the rest of the fields, auto-built from reflection. In HDR mode the procedural-only
                  //     sections (Sky Color / Sun & Sky / Clouds) don't apply, so we don't draw the reflected
                  //     block there — only the procedural mode gets the full sky/cloud authoring. ---
                  if ( skybox.Procedural )
                  {
                      // The sun's elevation comes from the scene's directional light (its Translation is the
                      // direction light TRAVELS, so the sun is the other way).
                      float sunElevation = 0.0f;
                      bool  haveSun      = false;
                      if ( scene )
                      {
                          auto view =
                               scene->GetRegistry().view<ECS::DirectionLightComponent, ECS::TransformComponent>();
                          for ( const auto e : view )
                          {
                              const glm::vec3 travel = view.template get<ECS::TransformComponent>( e ).Translation;
                              if ( glm::length( travel ) > 1e-4f )
                              {
                                  const glm::vec3 toSun = -glm::normalize( travel );
                                  sunElevation = glm::degrees( std::asin( glm::clamp( toSun.y, -1.0f, 1.0f ) ) );
                                  haveSun      = true;
                                  break;
                              }
                          }
                      }

                      if ( !ctx.FieldFilter )
                          DrawSkyRamp( skybox, sunElevation, haveSun );

                      PropertyEditorBuilder::Draw( &skybox, "SkyboxComponent", assetManager, ctx.UIHelper,
                                                   ctx.FieldFilter );
                  }

                  // ── Environment lighting (IBL) bake — the sky's contribution to scene lighting/reflections is
                  //    baked into cubemaps; this is a heavy device-idle op, so it's an explicit action. ──
                  ImGui::Spacing();
                  ImGui::Separator();
                  ImGui::TextUnformatted( "Environment Lighting (IBL)" );
                  ImGui::TextDisabled(
                       "Bake the sky into the irradiance / reflection maps used by PBR surfaces." );
                  if ( ImGui::Button( "Bake Sky IBL", ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
                      skybox.RequestBake = true;
              } ) )

} // namespace Desert::Editor

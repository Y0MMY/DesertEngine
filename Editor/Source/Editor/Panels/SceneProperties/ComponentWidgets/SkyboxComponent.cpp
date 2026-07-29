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
#include <Common/Utilities/FileSystem.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    // Most of the Skybox component's Details UI is AUTO-GENERATED from its REFLECT()/PROPERTY() metadata
    // (PropertyEditorBuilder). The ONE hand-drawn part is the HDR SkyboxAsset picker (a dropdown of loaded
    // skyboxes + drag-drop), because the generic reflected asset slot is texture-oriented and doesn't resolve
    // SkyboxAssets — so SkyboxHandle is marked Hidden and drawn here. Plus the "Bake Sky IBL" action button.
    DESERT_REGISTER_CUSTOM_COMPONENT(
         ECS::SkyboxComponent, "Skybox", false,
         ( []( ECS::Entity& entity, ::Desert::Core::Scene*, const ComponentEditContext& ctx )
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
                   std::string currentName = current ? Common::Utils::FileSystem::GetFileName(
                                                            current->GetMetadata().Filepath )
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
                   PropertyEditorBuilder::Draw( &skybox, "SkyboxComponent", assetManager, ctx.UIHelper );

               // ── Environment lighting (IBL) bake — the sky's contribution to scene lighting/reflections is
               //    baked into cubemaps; this is a heavy device-idle op, so it's an explicit action. ──
               ImGui::Spacing();
               ImGui::Separator();
               ImGui::TextUnformatted( "Environment Lighting (IBL)" );
               ImGui::TextDisabled( "Bake the sky into the irradiance / reflection maps used by PBR surfaces." );
               if ( ImGui::Button( "Bake Sky IBL", ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
                   skybox.RequestBake = true;
           } ) )

} // namespace Desert::Editor

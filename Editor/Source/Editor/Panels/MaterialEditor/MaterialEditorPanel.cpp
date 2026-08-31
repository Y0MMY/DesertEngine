#include "MaterialEditorPanel.hpp"

#include "MaterialShaderRebuild.hpp"

#include <Editor/Core/DragPayloads.hpp>
#include <Editor/Import/TextureDnD.hpp>
#include <Editor/Widgets/ThumbnailCache.hpp>
#include <Editor/Widgets/ThumbnailService.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Graphic/Materials/DataDrivenMaterial.hpp>
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/Material/MaterialService.hpp>
#include <Engine/Runtime/Services/Shader/ShaderService.hpp>

#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Desert::Editor
{
    // Inside Desert::* the unqualified name ImGui resolves to the engine's Desert::ImGui wrapper — alias the
    // real Dear ImGui back in (same trick the other panels use).
    namespace ImGui = ::ImGui;

    namespace
    {
        // The preview renders offscreen at a fixed size. Set ONCE rather than per frame: SceneRenderer's
        // Resize recreates every frame buffer and idles the GPU, so following the ImGui window's size would
        // stall the editor on every drag of the window edge.
        constexpr uint32_t kPreviewRenderSize = 512;

        const char* ShapeName( PreviewViewport::Shape s )
        {
            switch ( s )
            {
                case PreviewViewport::Shape::Sphere:
                    return "Sphere";
                case PreviewViewport::Shape::Cube:
                    return "Cube";
                case PreviewViewport::Shape::Plane:
                    return "Plane";
            }
            return "Sphere";
        }

        // What a domain is CALLED in this window. Written out rather than reflected off the enum: this is
        // artist-facing copy that has to read as English beside a material's name, and "Unspecified" is a
        // parser state, not a thing to tell somebody.
        const char* DomainName( ::Desert::Core::Formats::ShaderDomain domain )
        {
            using D = ::Desert::Core::Formats::ShaderDomain;
            switch ( domain )
            {
                case D::Unspecified:
                    return "engine-internal";
                case D::Surface:
                    return "Surface";
                case D::Terrain:
                    return "Terrain";
                case D::Skybox:
                    return "Skybox";
                case D::PostProcess:
                    return "Post Process";
            }
            return "engine-internal";
        }

        // What a shader of this domain draws INSTEAD, as a clause following the shader's own name. The half
        // of "no preview here" that is actually useful: being told a pane cannot show something is only
        // half an answer while nobody says where to look for it.
        //
        // ASCII ONLY, here and in every other string this file puts on screen. The editor's fonts are built
        // with GetGlyphRangesCyrillic() (EditorResources.cpp), whose Latin block stops at U+00FF: an em dash
        // or an ellipsis character is outside it and draws as a missing glyph. A message explaining a blank
        // pane cannot afford to be the thing that renders wrong.
        const char* WhatTheDomainDrawsInstead( ::Desert::Core::Formats::ShaderDomain domain )
        {
            using D = ::Desert::Core::Formats::ShaderDomain;
            switch ( domain )
            {
                case D::Terrain:
                    return "draws the scene's terrain and its grass, geometry the renderer synthesizes "
                           "itself, so there is no sphere, cube or plane this pane could put it on. Edit "
                           "it here and look at the terrain in the viewport.";
                case D::Skybox:
                    return "draws the sky behind everything else, not an object this pane could place.";
                case D::PostProcess:
                    return "draws over the whole finished frame, not an object this pane could place.";
                case D::Unspecified:
                case D::Surface:
                    break;
            }
            // Surface never reaches here (it is the one domain the pane CAN draw), so this is the
            // Unspecified case: a shader with no `Domain` line at all, which the engine treats as internal.
            return "declares no material domain, so the engine treats it as internal. No material should be "
                   "pointing at it at all.";
        }

        // The window's display name is the material's file stem, taken ONCE — the title carries the ImGui
        // window id (see AssetDocumentTitle) and a title that changed under a live window would orphan its
        // saved dock entry. The id half is the handle, so the label is free to be a human name without being
        // load-bearing.
        std::string MaterialDocumentName( const Assets::AssetHandle&                   material,
                                          const std::shared_ptr<Assets::AssetManager>& assetManager )
        {
            if ( assetManager )
            {
                if ( auto asset = assetManager->FindByHandle<Assets::SurfaceMaterialAsset>( material ) )
                {
                    const auto path = asset->GetMetadata().Filepath;
                    if ( !path.empty() )
                        return path.stem().string();
                }
            }
            // Named by handle rather than "Material": two unnamed materials must still read as two windows.
            return "Material " + std::to_string( static_cast<uint64_t>( material ) );
        }
    } // namespace

    MaterialEditorPanel::MaterialEditorPanel( const Assets::AssetHandle&                   material,
                                              const std::shared_ptr<Assets::AssetManager>& assetManager )
         : IAssetEditorPanel( MaterialDocumentName( material, assetManager ), material,
                              Assets::AssetTypeID::Material ),
           m_AssetManager( assetManager )
    {
        // Start level with the world: a rebuild that happened before this window existed left nothing here to
        // invalidate, and treating it as pending would drop pipelines that were never built.
        m_SeenRebuildCount = MaterialShaderRebuild::CountFor( EffectiveShaderName() );
    }

    // Written out rather than left to the members' reverse-declaration order. The two objects have to go in
    // a stated order — the UIHelper's descriptor sets reference the preview's images — and a teardown order
    // that depends on which line a member happens to be declared on is the shape of defect this engine has
    // paid for in Vulkan lifetimes more than once.
    MaterialEditorPanel::~MaterialEditorPanel()
    {
        ReleasePreview();
    }

    std::string MaterialEditorPanel::EffectiveShaderName() const
    {
        if ( !m_AssetManager )
            return {};
        auto asset = m_AssetManager->FindByHandle<Assets::SurfaceMaterialAsset>( Subject() );
        if ( !asset )
            return {};

        // Through the parent for an instance — see the header. MaterialData::EffectiveShaderName() answers
        // "StaticMeshPBR" for a material that names none, which is right for a base asset and wrong for a
        // child, whose shader is simply somewhere else.
        if ( auto parent = ResolveParent( *asset ) )
            return parent->Data().EffectiveShaderName();
        return asset->Data().EffectiveShaderName();
    }

    std::string MaterialEditorPanel::PreviewUnavailableReason( const std::string& shaderName ) const
    {
        auto* shaderService = Runtime::ResourceRegistry::GetShaderService();
        if ( !shaderService )
            return "No preview: the shader service is not running, so nothing can be resolved to draw with.";

        auto shader = shaderService->GetByName( shaderName );
        if ( !shader )
            return "No preview: the shader '" + shaderName +
                   "' is not loaded, so there is nothing to draw this material with.";

        // Registered-but-uncompiled is a real state and a deliberate one: ShaderService keeps the NAME of a
        // shader that failed to compile so the material does not quietly fall back to the standard one.
        // MeshRenderer then skips the draw outright, which is why the pane would be empty for a reason that
        // has nothing to do with this material's own values.
        if ( !shader->IsCompiled() )
            return "No preview: the shader '" + shaderName +
                   "' is registered but has no compiled stages, so nothing draws with it. The compile "
                   "error is in the Logs panel, named after this shader.";

        const auto domain = shader->GetProgramMeta().Domain;
        if ( domain != ::Desert::Core::Formats::ShaderDomain::Surface )
            return std::string( "No preview shape for a " ) + DomainName( domain ) + "-domain material.\n\n'" +
                   shaderName + "' " + WhatTheDomainDrawsInstead( domain ) +
                   "\n\nThe parameters beside it edit the material normally.";

        return {};
    }

    void MaterialEditorPanel::DrawPreviewPlaceholder( float side, const std::string& reason ) const
    {
        // Local (window) coordinates as well as screen ones: the draw list wants screen, and both the text
        // wrap position and the cursor restore below are window-space. Mixing the two puts the wrap column
        // hundreds of pixels off and the message renders as one unbroken line.
        const ImVec2 local  = ImGui::GetCursorPos();
        const ImVec2 screen = ImGui::GetCursorScreenPos();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled( screen, ImVec2( screen.x + side, screen.y + side ), IM_COL32( 28, 28, 32, 255 ), 4.0f );
        dl->AddRect( screen, ImVec2( screen.x + side, screen.y + side ), IM_COL32( 68, 68, 76, 255 ), 4.0f );

        constexpr float kPad = 14.0f;
        ImGui::SetCursorPos( ImVec2( local.x + kPad, local.y + kPad ) );
        ImGui::PushTextWrapPos( local.x + side - kPad );
        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.74f, 0.74f, 0.78f, 1.0f ) );
        ImGui::TextUnformatted( reason.c_str() );
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();

        // Back to the top-left corner and claim exactly the square an image would have claimed. Without
        // this the parameter column beside the pane moves with the length of the message.
        ImGui::SetCursorPos( local );
        ImGui::Dummy( ImVec2( side, side ) );
    }

    void MaterialEditorPanel::EnsurePreview()
    {
        if ( m_Preview )
            return;

        m_Preview  = std::make_unique<PreviewViewport>();
        m_UIHelper = std::make_unique<UI::UIHelper>();
        m_UIHelper->Init();
        m_Applied = false; // the new viewport knows nothing about the current material
    }

    void MaterialEditorPanel::ReleasePreview()
    {
        if ( !m_Preview )
            return;

        // ~PreviewViewport idles the device and releases the scene before the renderer, which is what
        // returns the renderer slot. Dropping the UIHelper too: its descriptor sets reference images that
        // belonged to the framebuffers just destroyed.
        m_Preview.reset();
        m_UIHelper.reset();
        m_Applied = false;
    }

    void MaterialEditorPanel::OnPreUpdate()
    {
        // THE SLOT IS NOT CLAIMED UNTIL THE WINDOW HAS ACTUALLY BEEN DRAWN. The document is created in
        // EditorLayer::ServiceAssetOpenRequests, which runs earlier in this same OnUpdate — so on the frame a
        // material is opened there is a panel but no window on screen yet, and building a Scene and a
        // SceneRenderer for it then would spend one of the six on something nobody has seen.
        //
        // The predecessor of this panel also RELEASED here, on the frame it stopped being visible. That
        // branch is gone rather than carried over: an asset document is not hidden when it is closed, it is
        // DESTROYED (EditorLayer::CloseDismissedAssetDocuments, which runs before this loop and so before
        // any such branch could fire), and one mechanism that runs is worth more than a second that cannot.
        if ( !m_DrewThisFrame )
            return;
        m_DrewThisFrame = false;

        // A material the pane cannot draw does not get a renderer slot. RELEASED rather than merely not
        // created: the shader behind a material is editable under a live window (the Node Graph rewrites
        // its scratch material's shader, and a `.demat` can be reloaded from disk), so a window that
        // already holds a slot has to give it back when its material moves to a domain with no shape —
        // otherwise one of the six is held for ever by a window showing a paragraph of text.
        if ( !m_PreviewUnavailable.empty() )
        {
            ReleasePreview();
            return;
        }

        EnsurePreview();

        if ( !m_Applied )
        {
            m_Preview->SetMaterial( Subject(), m_Shape );
            m_Applied = true;
        }

        // The shader behind this material was rebuilt: drop the pipelines THIS renderer cached from the old
        // modules. Without it the window keeps drawing the old shader after a recompile — see the note in
        // MaterialShaderRebuild.hpp.
        const std::string shaderName = EffectiveShaderName();
        if ( const uint64_t rebuilds = MaterialShaderRebuild::CountFor( shaderName );
             rebuilds != m_SeenRebuildCount )
        {
            m_SeenRebuildCount = rebuilds;
            if ( auto* shaderService = Runtime::ResourceRegistry::GetShaderService() )
            {
                if ( auto shader = shaderService->GetByName( shaderName ) )
                    m_Preview->InvalidatePipelines( shader.get() );
            }
        }

        m_Preview->Update( kPreviewRenderSize, kPreviewRenderSize );
    }

    void MaterialEditorPanel::DrawToolbar( Assets::SurfaceMaterialAsset* asset, bool isInstance )
    {
        // No material combo: this window IS one material. Picking a different one is opening a different
        // document, which is the whole point of the change that deleted the combo.
        ImGui::SetNextItemWidth( 110.0f );
        if ( ImGui::BeginCombo( "Shape", ShapeName( m_Shape ) ) )
        {
            for ( auto s :
                  { PreviewViewport::Shape::Sphere, PreviewViewport::Shape::Cube, PreviewViewport::Shape::Plane } )
            {
                const bool selected = ( s == m_Shape );
                if ( ImGui::Selectable( ShapeName( s ), selected ) && !selected )
                {
                    m_Shape   = s;
                    m_Applied = false; // re-push so the primitive actually changes
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if ( ImGui::Button( "Reset View" ) && m_Preview )
            m_Preview->ResetView();

        if ( !asset )
            return;

        // Save came here with the parameters it persists. It used to sit on the Details slot row, where it
        // was the only way to write a `.demat` at all — but Details no longer edits a material, so a save
        // button there would be an action with nothing to save, and this window would be an editor whose
        // edits die with the session.
        ImGui::SameLine();
        if ( ImGui::Button( "Save" ) )
            SaveSubject( *asset );

        if ( isInstance )
        {
            ImGui::SameLine();
            if ( ImGui::Button( "Reset Overrides" ) )
            {
                asset->Data().Params.clear();
                asset->Data().Textures.clear();
                PropagateEdit( *asset, /*isInstance=*/true );
            }
            if ( ImGui::IsItemHovered() )
                // ASCII: an em dash here draws as '?' — measured, see WhatTheDomainDrawsInstead.
                ImGui::SetTooltip( "Drop every override, back to the parent's values" );
        }
    }

    std::shared_ptr<Assets::SurfaceMaterialAsset>
    MaterialEditorPanel::ResolveParent( const Assets::SurfaceMaterialAsset& asset ) const
    {
        if ( !m_AssetManager || !asset.Data().IsInstance() )
            return nullptr;

        // The child references its parent by the parent's STABLE in-file id, not by an asset handle — that
        // is what survives the file being moved — so the service's external->internal map is the only way
        // back to a loaded asset.
        auto* materialService = Runtime::ResourceRegistry::GetMaterialService();
        if ( !materialService )
            return nullptr;

        const auto parentHandle = materialService->GetAssetHandleByExternal( *asset.Data().ParentMaterialId );
        if ( parentHandle.IsNull() )
            return nullptr;
        return m_AssetManager->FindByHandle<Assets::SurfaceMaterialAsset>( parentHandle );
    }

    bool MaterialEditorPanel::DrawShaderPicker( Assets::SurfaceMaterialAsset& asset )
    {
        auto* shaderService = Runtime::ResourceRegistry::GetShaderService();
        if ( !shaderService )
        {
            ImGui::TextDisabled( "Shader: the shader service is not running" );
            return false;
        }

        const std::string current = asset.Data().EffectiveShaderName();

        // THE DOMAIN COMES FROM THE MATERIAL, and everything below follows from it. See the header for
        // what the hardcoded `Surface` filter did to a Terrain material.
        auto currentShader = shaderService->GetByName( current );
        if ( !currentShader )
        {
            // No shader, no domain, and therefore no honest list. Falling back to "well, Surface then"
            // would be the whole defect again: a click would silently rehome a material whose real domain
            // nobody in this process can currently read.
            ImGui::TextDisabled( "Shader" );
            ImGui::TextDisabled( "'%s' is not loaded, so its domain is unknown and no list can be offered",
                                 current.c_str() );
            return false;
        }

        // IsUserAssignable() is asked of the MATERIAL'S OWN shader, not of the candidates: inside one
        // domain every entry is assignable by construction, so asking it there would be a tautology. Here
        // it answers the question that is not — whether this material sits in a domain a user authors
        // materials in at all. A `.demat` naming a Skybox shader, or one with no `Domain` line, is already
        // drawing nothing; the list it needs is not "more of the same domain".
        const bool assignable = currentShader->GetProgramMeta().IsUserAssignable();
        const auto domain     = currentShader->GetProgramMeta().Domain;

        if ( assignable )
        {
            ImGui::TextDisabled( "Shader  (%s domain)", DomainName( domain ) );
        }
        else
        {
            ImGui::TextDisabled( "Shader" );
            ImGui::TextColored( ImVec4( 1.0f, 0.72f, 0.35f, 1.0f ),
                                "'%s' is not a material shader (%s). Pick a real one below.", current.c_str(),
                                DomainName( domain ) );
        }

        // Sorted, because GetAllNames() walks an unordered_map: without this the same project shows the
        // same shaders in a different order every run, and the entry under the cursor moves between
        // sessions.
        std::vector<std::string> names = shaderService->GetAllNames();
        std::sort( names.begin(), names.end() );

        bool shaderChanged = false;
        ImGui::SetNextItemWidth( -FLT_MIN );
        const bool open    = ImGui::BeginCombo( "##shader", current.c_str() );
        const bool hovered = ImGui::IsItemHovered(); // the combo itself; after EndCombo this is the popup
        if ( open )
        {
            for ( const auto& name : names )
            {
                auto candidate = shaderService->GetByName( name );
                if ( !candidate )
                    continue;

                const auto& meta = candidate->GetProgramMeta();
                // Same domain when the material has one; every assignable domain when it does not, which
                // is the only way out of a material pointing at an engine shader.
                const bool offer = assignable ? ( meta.Domain == domain ) : meta.IsUserAssignable();
                if ( !offer && name != current )
                    continue;

                // The current entry is listed even when it would not otherwise qualify. A combo that
                // cannot reproduce the value it is displaying is the defect this function was rewritten
                // for, and a shader that fails to compile keeps its name precisely so it stays visible.
                const bool selected = ( name == current );
                const std::string label    = candidate->IsCompiled() ? name : name + "  (does not compile)";

                if ( ImGui::Selectable( label.c_str(), selected ) && !selected )
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

        if ( hovered )
        {
            if ( assignable )
                ImGui::SetTooltip( "%s-domain shaders only.\nA material's domain decides how the renderer "
                                   "draws it at all; a shader from another domain would leave these "
                                   "parameters naming uniform fields it does not have, and nothing further "
                                   "down rejects that.",
                                   DomainName( domain ) );
            else
                ImGui::SetTooltip( "Every shader a material may use.\n'%s' is not one of them, so this list "
                                   "is the repair rather than the usual same-domain choice.",
                                   current.c_str() );
        }
        return shaderChanged;
    }

    bool MaterialEditorPanel::DrawParameters( Assets::SurfaceMaterialAsset& asset,
                                              const Assets::MaterialData* parentData, bool isInstance )
    {
        auto*             shaderService = Runtime::ResourceRegistry::GetShaderService();
        const std::string shaderName =
             parentData ? parentData->EffectiveShaderName() : asset.Data().EffectiveShaderName();
        auto shader = shaderService ? shaderService->GetByName( shaderName ) : nullptr;
        if ( !shader )
        {
            ImGui::TextDisabled( "Shader '%s' is not loaded", shaderName.c_str() );
            return false;
        }

        const auto& schema = shader->GetProgramMeta();
        if ( schema.Params.empty() )
        {
            ImGui::TextDisabled( "This shader exposes no parameters" );
            return false;
        }

        auto& data    = asset.Data();
        bool  changed = false;

        // Editing a value writes it into the material asset and nothing else: no recompile, no pipeline
        // rebuild. A parameter is a uniform-buffer field, so the cost of dragging this slider is the memcpy
        // MeshRenderer already does every frame — which is why parameters update LIVE while a change to the
        // graph's topology is debounced. Measured: a shader that has to be rebuilt costs ~123 ms per SPIR-V
        // module on this machine, and a Surface graph emits three.
        // Two columns, label cell then control cell. Drawing the label as ImGui's own trailing label instead
        // put it on top of the value: a colour row came out reading "A255e255 255 255", which is what
        // looking at the panel found and reading the code did not.
        if ( !ImGui::BeginTable( "##material_params", 2,
                                 ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings ) )
            return false;
        ImGui::TableSetupColumn( "label", ImGuiTableColumnFlags_WidthStretch, 0.45f );
        ImGui::TableSetupColumn( "control", ImGuiTableColumnFlags_WidthStretch, 0.55f );

        for ( const auto& p : schema.Params )
        {
            using W  = ::Desert::Core::Formats::ShaderParamWidget;
            using VT = ::Desert::Core::Formats::ShaderValueType;

            const char*       label    = p.DisplayName.empty() ? p.Name.c_str() : p.DisplayName.c_str();
            const std::string hiddenId = "##mp_" + p.Name; // control id; the label lives in its own cell

            // Instance mode: a row with its own entry in the child IS an override — mark it.
            const bool overridden = isInstance && !p.IsTexture && data.FindParam( p.Name ) != nullptr;

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
                            const auto resolved =
                                 ::Desert::Editor::TextureDnD::ResolveOrImport( *m_AssetManager, path );
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
                    float mn = *p.Min;
                    float mx = *p.Max;
                    edited =
                         ImGui::SliderScalarN( hiddenId.c_str(), ImGuiDataType_Float, &value.x, comps, &mn, &mx );
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

    void MaterialEditorPanel::PropagateEdit( Assets::SurfaceMaterialAsset& asset, bool isInstance )
    {
        auto* materialService = Runtime::ResourceRegistry::GetMaterialService();
        if ( !materialService )
            return;

        // ONE propagation path for both audiences. This window's preview target is an ordinary
        // StaticMeshComponent holding this material in a SLOT, exactly like a mesh in the level, so
        // whatever reaches the scene reaches the preview by the same wire — which is the only way the two
        // can be guaranteed to agree (Docs/MaterialEditor/STAGE1_END_TO_END.md).
        if ( isInstance )
        {
            // An instance has no runtime Material of its own — its overrides are re-applied when a cached
            // instance set is rebuilt, so the stamp is the whole mechanism here.
            materialService->BumpInvalidationVersion();
            return;
        }

        // The runtime Material is built ONCE and cached by the service; rebuilding an instance of it would
        // faithfully reproduce the old values. Apply* is what pushes the new ones in — and it is also what
        // re-binds textures, which is why binding a texture here shows on the mesh without a reload.
        if ( auto* runtime = materialService->Get( asset.GetMetadata().Handle ) )
        {
            if ( auto* pbr = dynamic_cast<Graphic::StaticMaterialPBR*>( runtime ) )
                Graphic::MaterialFactory::ApplyPBRAsset( *pbr, asset );
            else if ( auto* ddm = dynamic_cast<Graphic::DataDrivenMaterial*>( runtime ) )
                Graphic::MaterialFactory::ApplyShaderAsset( *ddm, asset );
        }

        // ...and the stamp, because entities rendering through CHILD instances of this material cache
        // their own override sets and would otherwise keep the values from before.
        materialService->BumpInvalidationVersion();
    }

    void MaterialEditorPanel::SaveSubject( Assets::SurfaceMaterialAsset& asset )
    {
        const auto        path = asset.GetMetadata().Filepath;
        const std::string text = asset.Save();
        Common::Utils::FileSystem::WriteContentToFile( path, text );

        // WriteContentToFile reports nothing, so the write is confirmed by looking at the result. Without
        // this a read-only file or a missing directory would leave the user with a Save button that appears
        // to work and a material whose edits die with the session — and the thumbnail below would be
        // discarded for a change that never landed.
        std::error_code      sizeEc;
        const std::uintmax_t written = std::filesystem::file_size( path, sizeEc );
        if ( sizeEc || written != text.size() )
        {
            LOG_ERROR( "[MaterialEditor] '{}' was not written ({} bytes on disk, {} expected): this "
                       "material's edits are still only in memory.",
                       path.generic_string(), sizeEc ? 0ull : static_cast<uint64_t>( written ), text.size() );
            return;
        }

        // Drop ONLY this material's rendered thumbnail so every panel showing it re-renders with the new
        // look immediately. Deleted rather than left to the browser's modtime check: that check ignores a
        // source newer by less than three seconds (coarse filesystem timestamps otherwise report a PNG as
        // stale the moment it is written), so a material saved shortly after its thumbnail was captured
        // would keep showing the old one for the rest of the session.
        std::error_code   ec;
        const std::string png = ThumbnailCache::DiskPath( path.generic_string() );
        std::filesystem::remove( png, ec );
        ThumbnailService::Get().Invalidate( path.generic_string() );
    }

    void MaterialEditorPanel::OnUIRender()
    {
        // Resolved ONCE per frame and handed to everything below. The subject is a handle, not a pointer, so
        // the asset can go away under an open window (deleted on disk, project reloaded); every part of the
        // window then has to agree about that, and re-looking it up per section is how two halves of one
        // window come to disagree.
        auto asset =
             m_AssetManager ? m_AssetManager->FindByHandle<Assets::SurfaceMaterialAsset>( Subject() ) : nullptr;
        auto       parent     = asset ? ResolveParent( *asset ) : nullptr;
        const bool isInstance = asset && asset->Data().IsInstance();

        // Decided HERE, once, for both the pane below and next frame's OnPreUpdate — which is the only
        // place allowed to build or destroy the renderer. Resolving it twice is how the window would come
        // to hold a slot while telling the artist it has no preview, or the reverse.
        m_PreviewUnavailable = asset ? PreviewUnavailableReason( EffectiveShaderName() )
                                     : std::string( "No preview: this material is no longer loaded." );

        DrawToolbar( asset.get(), isInstance );
        ImGui::Separator();

        const float  kParamColumnW = 300.0f;
        const ImVec2 avail         = ImGui::GetContentRegionAvail();
        const float  imageSide     = std::max( 64.0f, std::min( avail.x - kParamColumnW, avail.y ) );

        // The image is last frame's render; recording this frame's happens in OnPreUpdate. Rendering from
        // inside the ImGui pass destroys descriptor pools whose sets are bound to the command buffer being
        // recorded — the editor has been bitten by exactly that.
        if ( m_PreviewUnavailable.empty() && m_Preview && m_UIHelper && m_Preview->HasContent() )
        {
            m_Preview->Draw( *m_UIHelper, ImVec2( imageSide, imageSide ) );
        }
        else
        {
            // The empty reason is the one frame between the window first drawing and OnPreUpdate building
            // its viewport. Said out loud rather than left as a blank rectangle: "starting" and "there is
            // nothing to show you" look identical, and only one of them is worth waiting for.
            DrawPreviewPlaceholder( imageSide, m_PreviewUnavailable.empty()
                                                    ? std::string( "Starting the preview..." )
                                                    : m_PreviewUnavailable );
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        if ( !asset )
        {
            ImGui::TextDisabled( "This material is no longer loaded" );
        }
        else
        {
            // Instance parenting is stated where the overrides are edited, and only here: a starred row in
            // the table below means "this child overrides the parent", which is unreadable without knowing
            // there is a parent at all.
            if ( isInstance )
            {
                const std::string parentName =
                     parent ? std::filesystem::path( parent->GetMetadata().Filepath ).stem().string()
                            : std::string( "<missing parent>" );
                ImGui::TextDisabled( "Instance of %s", parentName.c_str() );
                ImGui::Separator();
            }
            else
            {
                // The shader lives INSIDE the material (Unity's model, and ours). An instance never gets a
                // picker: it always renders with its parent chain's shader. The caption is the picker's
                // own, because it names the domain the list is filtered to and that is not knowable here.
                if ( DrawShaderPicker( *asset ) )
                {
                    // A different shader means a different runtime material CLASS — the cached one cannot be
                    // re-valued, it has to be dropped so the next Get rebuilds it from the asset. Invalidate
                    // bumps the stamp itself, so every mesh rebuilds its instances with it.
                    if ( auto* materialService = Runtime::ResourceRegistry::GetMaterialService() )
                        materialService->Invalidate( asset->GetMetadata().Handle );
                }
                ImGui::Separator();
            }

            ImGui::TextDisabled( "Parameters" );
            ImGui::Separator();
            if ( DrawParameters( *asset, parent ? &parent->Data() : nullptr, isInstance ) )
                PropagateEdit( *asset, isInstance );
        }
        ImGui::EndGroup();

        // Claim the render for next frame's OnPreUpdate. Re-affirmed every frame on purpose: stop drawing
        // and the GPU work stops with it.
        m_DrewThisFrame = true;
    }
} // namespace Desert::Editor

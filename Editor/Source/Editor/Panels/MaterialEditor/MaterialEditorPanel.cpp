#include "MaterialEditorPanel.hpp"

#include "MaterialShaderRebuild.hpp"

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <algorithm>
#include <string>

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
        return asset ? asset->Data().EffectiveShaderName() : std::string{};
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

    void MaterialEditorPanel::DrawToolbar()
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
    }

    void MaterialEditorPanel::DrawParameters()
    {
        if ( !m_AssetManager )
            return;

        auto asset = m_AssetManager->FindByHandle<Assets::SurfaceMaterialAsset>( Subject() );
        if ( !asset )
        {
            ImGui::TextDisabled( "This material is no longer loaded" );
            return;
        }

        auto* shaderService = Runtime::ResourceRegistry::GetShaderService();
        auto  shader = shaderService ? shaderService->GetByName( asset->Data().EffectiveShaderName() ) : nullptr;
        if ( !shader )
        {
            ImGui::TextDisabled( "Shader '%s' is not loaded", asset->Data().EffectiveShaderName().c_str() );
            return;
        }

        const auto& schema = shader->GetProgramMeta();
        if ( schema.Params.empty() )
        {
            ImGui::TextDisabled( "This shader exposes no parameters" );
            return;
        }

        // Editing a value writes it into the material asset and nothing else: no recompile, no pipeline
        // rebuild. A parameter is a uniform-buffer field, so the cost of dragging this slider is the memcpy
        // MeshRenderer already does every frame — which is why parameters update LIVE while a change to the
        // graph's topology is debounced. Measured: a shader that has to be rebuilt costs ~123 ms per SPIR-V
        // module on this machine, and a Surface graph emits three.
        // Two columns, label cell then control cell — the same shape the Details panel's material editor
        // uses. Drawing the label as ImGui's own trailing label instead put it on top of the value: a
        // colour row came out reading "A255e255 255 255", which is what looking at the panel found and
        // reading the code did not.
        if ( !ImGui::BeginTable( "##material_params", 2,
                                 ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings ) )
            return;
        ImGui::TableSetupColumn( "label", ImGuiTableColumnFlags_WidthStretch, 0.45f );
        ImGui::TableSetupColumn( "control", ImGuiTableColumnFlags_WidthStretch, 0.55f );

        for ( const auto& p : schema.Params )
        {
            if ( p.IsTexture )
                continue;

            using VT = ::Desert::Core::Formats::ShaderValueType;
            using W  = ::Desert::Core::Formats::ShaderParamWidget;

            const char*       label    = p.DisplayName.empty() ? p.Name.c_str() : p.DisplayName.c_str();
            const std::string hiddenId = "##mp_" + p.Name; // the label lives in its own cell
            glm::vec4         value    = asset->Data().GetParam( p.Name, p.Default );
            bool              edited   = false;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( label );
            ImGui::TableNextColumn();
            ImGui::PushItemWidth( -FLT_MIN );

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
                    edited =
                         ImGui::SliderScalarN( hiddenId.c_str(), ImGuiDataType_Float, &value.x, comps, &mn, &mx );
                }
                else
                {
                    edited = ImGui::DragScalarN( hiddenId.c_str(), ImGuiDataType_Float, &value.x, comps, 0.01f );
                }
            }
            ImGui::PopItemWidth();

            if ( edited )
            {
                asset->Data().SetParam( p.Name, value );
                // Re-push the material so MeshECSSystem rebuilds the runtime instance through
                // MaterialFactory against the new values — the same thing the Details panel's Invalidate()
                // does for a scene mesh. No recompile: a parameter is a uniform-buffer field.
                m_Applied = false;
            }
        }
        ImGui::EndTable();
    }

    void MaterialEditorPanel::OnUIRender()
    {
        DrawToolbar();
        ImGui::Separator();

        const float  kParamColumnW = 300.0f;
        const ImVec2 avail         = ImGui::GetContentRegionAvail();
        const float  imageSide     = std::max( 64.0f, std::min( avail.x - kParamColumnW, avail.y ) );

        // The image is last frame's render; recording this frame's happens in OnPreUpdate. Rendering from
        // inside the ImGui pass destroys descriptor pools whose sets are bound to the command buffer being
        // recorded — the editor has been bitten by exactly that.
        if ( m_Preview && m_UIHelper && m_Preview->HasContent() )
        {
            m_Preview->Draw( *m_UIHelper, ImVec2( imageSide, imageSide ) );
        }
        else
        {
            ImDrawList*  dl = ImGui::GetWindowDrawList();
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            dl->AddRectFilled( p0, ImVec2( p0.x + imageSide, p0.y + imageSide ), IM_COL32( 28, 28, 32, 255 ),
                               4.0f );
            ImGui::Dummy( ImVec2( imageSide, imageSide ) );
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextDisabled( "Parameters" );
        ImGui::Separator();
        DrawParameters();
        ImGui::EndGroup();

        // Claim the render for next frame's OnPreUpdate. Re-affirmed every frame on purpose: stop drawing
        // and the GPU work stops with it.
        m_DrewThisFrame = true;
    }
} // namespace Desert::Editor

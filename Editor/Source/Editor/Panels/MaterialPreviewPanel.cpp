#include "MaterialPreviewPanel.hpp"

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/Logger.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace Desert::Editor
{
    // Inside Desert::* the unqualified name ImGui resolves to the engine's Desert::ImGui wrapper — alias the
    // real Dear ImGui back in (same trick the other panels use).
    namespace ImGui = ::ImGui;

    namespace
    {
        // One pending request of each kind is plenty: the last one wins, exactly like
        // NodeGraphPanel::RequestOpen.
        Assets::AssetHandle s_PendingMaterial{ static_cast<uint64_t>( 0 ) };
        bool                s_HasPendingMaterial = false;
        std::string         s_PendingRebuiltShader;

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

        std::string MaterialLabel( const Assets::Asset<Assets::SurfaceMaterialAsset>& asset )
        {
            if ( !asset )
                return "<none>";
            const auto path = asset->GetMetadata().Filepath;
            return path.empty() ? std::string( "<unnamed>" ) : path.stem().string();
        }
    } // namespace

    MaterialPreviewPanel::MaterialPreviewPanel( const std::shared_ptr<Assets::AssetManager>& assetManager )
         : IPanel( "Material Preview", /*showPanel=*/false ), m_AssetManager( assetManager )
    {
    }

    MaterialPreviewPanel::~MaterialPreviewPanel() = default;

    void MaterialPreviewPanel::RequestPreview( const Assets::AssetHandle& material )
    {
        s_PendingMaterial    = material;
        s_HasPendingMaterial = true;
    }

    void MaterialPreviewPanel::RequestShaderRebuilt( const std::string& shaderName )
    {
        s_PendingRebuiltShader = shaderName;
    }

    void MaterialPreviewPanel::EnsurePreview()
    {
        if ( m_Preview )
            return;

        m_Preview  = std::make_unique<PreviewViewport>();
        m_UIHelper = std::make_unique<UI::UIHelper>();
        m_UIHelper->Init();
        m_Applied = false; // the new viewport knows nothing about the current material
    }

    void MaterialPreviewPanel::ReleasePreview()
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

    void MaterialPreviewPanel::OnPreUpdate()
    {
        // OnPreUpdate runs for EVERY panel, including hidden ones — that is what makes this the right place
        // to give the slot back. A panel that only stopped rendering would still hold one of the six.
        if ( !m_SowPanel )
        {
            ReleasePreview();
            m_DrewThisFrame = false;
            return;
        }

        if ( s_HasPendingMaterial )
        {
            m_Material           = s_PendingMaterial;
            s_HasPendingMaterial = false;
            m_Applied            = false;
            m_SowPanel           = true;
        }

        // Opened with nothing to show: pick something rather than presenting a blank window. A material on
        // a CUSTOM shader is preferred because that is what this window exists for — it is the shader
        // graph's material editor — and only the standard PBR material otherwise. Deterministic, and the
        // combo is right there to change it.
        if ( static_cast<uint64_t>( m_Material ) == 0 && m_AssetManager )
        {
            const auto materials = m_AssetManager->FindAllByType<Assets::SurfaceMaterialAsset>();
            for ( const auto& [handle, asset] : materials )
            {
                if ( asset && asset->Data().UsesCustomShader() )
                {
                    m_Material = handle;
                    break;
                }
            }
            if ( static_cast<uint64_t>( m_Material ) == 0 && !materials.empty() )
                m_Material = materials.front().first;
            m_Applied = false;
        }

        if ( !m_DrewThisFrame )
        {
            // Open but not actually drawn last frame — a collapsed window or a background dock tab. Keep the
            // viewport (the user is one click from it) but record nothing.
            return;
        }
        m_DrewThisFrame = false;

        EnsurePreview();

        if ( !m_Applied && static_cast<uint64_t>( m_Material ) != 0 )
        {
            m_Preview->SetMaterial( m_Material, m_Shape );
            m_Applied = true;
        }

        // The shader behind this material was rebuilt: drop the pipelines THIS renderer cached from the old
        // modules. Without it the preview keeps drawing the old shader after a recompile — see the note on
        // RequestShaderRebuilt.
        if ( !s_PendingRebuiltShader.empty() )
        {
            if ( auto shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( s_PendingRebuiltShader ) )
                m_Preview->InvalidatePipelines( shader.get() );
            s_PendingRebuiltShader.clear();
        }

        if ( static_cast<uint64_t>( m_Material ) != 0 )
            m_Preview->Update( kPreviewRenderSize, kPreviewRenderSize );
    }

    void MaterialPreviewPanel::DrawToolbar()
    {
        ImGui::SetNextItemWidth( 220.0f );
        auto current = m_AssetManager ? m_AssetManager->FindByHandle<Assets::SurfaceMaterialAsset>( m_Material )
                                      : Assets::Asset<Assets::SurfaceMaterialAsset>{};
        if ( ImGui::BeginCombo( "Material", MaterialLabel( current ).c_str() ) )
        {
            if ( m_AssetManager )
            {
                for ( const auto& [handle, asset] : m_AssetManager->FindAllByType<Assets::SurfaceMaterialAsset>() )
                {
                    const bool selected = ( handle == m_Material );
                    if ( ImGui::Selectable( MaterialLabel( asset ).c_str(), selected ) && !selected )
                    {
                        m_Material = handle;
                        m_Applied  = false;
                    }
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
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

        if ( !m_Status.empty() )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", m_Status.c_str() );
        }
    }

    void MaterialPreviewPanel::DrawParameters()
    {
        if ( !m_AssetManager || static_cast<uint64_t>( m_Material ) == 0 )
            return;

        auto asset = m_AssetManager->FindByHandle<Assets::SurfaceMaterialAsset>( m_Material );
        if ( !asset )
            return;

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
        if ( !ImGui::BeginTable( "##preview_params", 2,
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
            const std::string hiddenId = "##pp_" + p.Name; // the label lives in its own cell
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

    void MaterialPreviewPanel::OnUIRender()
    {
        DrawToolbar();
        ImGui::Separator();

        const float  kParamColumnW = 300.0f;
        const ImVec2 avail         = ImGui::GetContentRegionAvail();
        const float  imageSide     = std::max( 64.0f, std::min( avail.x - kParamColumnW, avail.y ) );

        if ( static_cast<uint64_t>( m_Material ) == 0 )
        {
            ImGui::TextDisabled( "Pick a material, or press Compile in the Node Graph." );
            return;
        }

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

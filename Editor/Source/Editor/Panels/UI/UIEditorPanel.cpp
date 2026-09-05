#include "UIEditorPanel.hpp"
#include "UIElementCatalog.hpp"
#include "UIElementFactory.hpp"

#include <Editor/Panels/PanelContext.hpp>

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/UI/UILayout.hpp>
#include <Engine/UI/UICanvasRenderer2D.hpp>

#include <Common/Core/Logger.hpp>

#include <ImGui/imgui.h>

#include <algorithm>
#include <cmath>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
        // The preview target is the canvas's DESIGN resolution, not the panel's size, and that is what makes
        // the preview honest: at the reference resolution all three canvas scale modes coincide (Stretch is
        // 1:1, ScaleWithScreen scales by 1, Letterbox fits exactly), so the picture is the canvas as authored
        // rather than the canvas squeezed into whatever the user dragged the window to. The panel then just
        // fits that image into its content region.
        constexpr uint32_t kMinDesignPx = 16;
        constexpr uint32_t kMaxDesignPx = 4096;

        uint32_t ClampDesignPx( float v )
        {
            if ( !std::isfinite( v ) || v < static_cast<float>( kMinDesignPx ) )
                return kMinDesignPx;
            return std::min( static_cast<uint32_t>( v ), kMaxDesignPx );
        }
    } // namespace

    UIEditorPanel::UIEditorPanel( const std::shared_ptr<::Desert::Core::Scene>& scene )
         : IPanel( "UI Editor", /*showPanel=*/false ), m_Scene( scene )
    {
    }

    UIEditorPanel::~UIEditorPanel()
    {
        ReleaseTarget();
    }

    void UIEditorPanel::ReleaseTarget()
    {
        if ( !m_Target )
            return;
        Graphic::Renderer::GetInstance().WaitDeviceIdle();
        m_RenderPass.reset();
        m_Target.reset();
        m_TargetWidth  = 0;
        m_TargetHeight = 0;
    }

    bool UIEditorPanel::EnsureTarget( uint32_t width, uint32_t height )
    {
        if ( m_Target && m_TargetWidth == width && m_TargetHeight == height && m_Render2D.IsInitialized() )
            return true;

        ReleaseTarget();

        // RGBA32F, the format the scene's own composite target uses, so the UI2D/UIText/UIGlass shaders
        // write exactly the values they write in the game. What this preview does NOT have is the post
        // stack the game composites the canvas into, so it shows the canvas before tonemapping.
        Graphic::FramebufferSpecification spec;
        spec.DebugName   = "UIEditorPreview";
        spec.Width       = width;
        spec.Height      = height;
        spec.NoResizeble = true;
        spec.Attachments.Attachments.push_back( ::Desert::Core::Formats::ImageFormat::RGBA32F );

        m_Target = Graphic::Framebuffer::Create( spec );
        if ( !m_Target )
        {
            m_PreviewError = "could not create the preview framebuffer";
            LOG_ERROR( "[UI Editor] {} ({}x{})", m_PreviewError, width, height );
            return false;
        }
        if ( const auto result = m_Target->Resize( width, height, /*forceRecreate=*/true ); !result )
        {
            m_PreviewError = result.GetError();
            LOG_ERROR( "[UI Editor] preview framebuffer resize to {}x{} failed: {}", width, height,
                       m_PreviewError );
            m_Target.reset();
            return false;
        }

        Graphic::RenderPassSpecification passSpec;
        passSpec.TargetFramebuffer = m_Target;
        passSpec.DebugName         = "UIEditorPreview";
        // The authoring backdrop behind the canvas. It is editor chrome, not canvas content: the canvas
        // itself draws whatever its own panels say.
        passSpec.ClearColor.Color = { 0.094f, 0.098f, 0.118f, 1.0f };
        m_RenderPass              = Graphic::RenderPass::Create( passSpec );
        if ( !m_RenderPass )
        {
            m_PreviewError = "could not create the preview render pass";
            LOG_ERROR( "[UI Editor] {} ({}x{})", m_PreviewError, width, height );
            m_Target.reset();
            return false;
        }

        if ( const auto result = m_Render2D.Init( m_Target ); !result )
        {
            m_PreviewError = result.GetError();
            LOG_ERROR( "[UI Editor] preview Render2D init failed: {}", m_PreviewError );
            m_RenderPass.reset();
            m_Target.reset();
            return false;
        }

        m_TargetWidth  = width;
        m_TargetHeight = height;
        m_PreviewError.clear();
        LOG_INFO( "[UI Editor] preview target {}x{} (canvas design resolution)", width, height );
        return true;
    }

    void UIEditorPanel::OnPreUpdate()
    {
        m_PreviewRecorded = false;

        // A closed panel renders nothing and holds nothing: the target is released so a session that opened
        // the UI editor once does not keep a design-resolution framebuffer alive for the rest of it.
        if ( !m_SowPanel || !m_Scene )
        {
            ReleaseTarget();
            return;
        }

        auto&              reg    = m_Scene->GetRegistry();
        const entt::entity canvas = FindUICanvas( reg );
        if ( canvas == entt::null )
        {
            ReleaseTarget();
            return;
        }

        const auto& canvasData = reg.get<ECS::UICanvasComponent>( canvas ).Data;
        if ( !canvasData.Visible )
            return;

        const uint32_t w = ClampDesignPx( canvasData.ReferenceWidth );
        const uint32_t h = ClampDesignPx( canvasData.ReferenceHeight );
        if ( !EnsureTarget( w, h ) )
            return;

        const ::Desert::UI::Rect viewport{ 0.0f, 0.0f, static_cast<float>( w ), static_cast<float>( h ) };

        auto& renderer = Graphic::Renderer::GetInstance();
        renderer.BeginRenderPass( m_RenderPass.get(), /*clearFrame=*/true );
        m_Render2D.BeginFrame( { viewport.X, viewport.Y, viewport.W, viewport.H } );

        // input = nullptr is what makes the preview inert: buttons draw their normal state, nothing is
        // hovered, pressed, dragged or typed into, and no button action can fire from an authoring window.
        // It is the same "design mode" call EditorUIPass makes when Play-in-editor is off.
        //
        // worldViewProj = nullptr on purpose too: a WorldSpace canvas is billboarded by the camera in the
        // viewport, but there is no camera here — the authoring view shows it flat, at its design size.
        ::Desert::UI::RenderCanvas2D( reg, m_Render2D.GetDrawList(), viewport, /*worldViewProj=*/nullptr,
                            /*input=*/nullptr );
        m_Render2D.Flush();
        renderer.EndRenderPass();

        m_PreviewRecorded = true;
    }

    void UIEditorPanel::OnUIRender()
    {
        if ( !m_Scene )
        {
            ImGui::TextDisabled( "No active scene." );
            return;
        }

        auto&              reg    = m_Scene->GetRegistry();
        const entt::entity canvas = FindUICanvas( reg );

        if ( canvas == entt::null )
        {
            ImGui::TextDisabled( "No UI Canvas in the scene." );
            if ( ImGui::Button( ICON_MDI_PLUS " Create UI Canvas" ) )
            {
                auto& e = m_Scene->CreateNewEntity( "UI Canvas" );
                e.AddComponent<ECS::UICanvasComponent>();
            }
            return;
        }

        const auto& canvasData = reg.get<ECS::UICanvasComponent>( canvas ).Data;

        // Toolbar, generated from the one element catalog the viewport's "UI" menu also reads. Buttons wrap
        // to the next line instead of running off the edge — eleven of them do not fit a docked panel.
        {
            const ImGuiStyle& style      = ImGui::GetStyle();
            const float       rightEdge  = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            for ( std::size_t i = 0; i < kUIElementCount; ++i )
            {
                const UIElementEntry& entry = kUIElements[i];
                const std::string     label = std::string( entry.Icon ) + " + " + entry.Label;
                if ( ImGui::Button( label.c_str() ) )
                    CreateUIElement( *m_Scene, canvas, i );

                if ( i + 1 >= kUIElementCount )
                    break;
                const std::string next = std::string( kUIElements[i + 1].Icon ) + " + " + kUIElements[i + 1].Label;
                const float nextWidth = ImGui::CalcTextSize( next.c_str() ).x + style.FramePadding.x * 2.0f;
                if ( ImGui::GetItemRectMax().x + style.ItemSpacing.x + nextWidth < rightEdge )
                    ImGui::SameLine();
            }
        }
        ImGui::TextDisabled( "add UI elements, then edit anchors/colour in Details" );
        ImGui::Separator();

        if ( !canvasData.Visible )
        {
            ImGui::TextDisabled( "Canvas is hidden (UI Canvas -> Visible)." );
            return;
        }
        if ( !m_PreviewError.empty() )
        {
            ImGui::TextColored( ImVec4( 1.0f, 0.45f, 0.35f, 1.0f ), "Preview unavailable: %s",
                                m_PreviewError.c_str() );
            return;
        }

        // Fit the design-resolution image into the content region, centred, preserving its aspect.
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail  = ImGui::GetContentRegionAvail();
        if ( avail.x < 1.0f || avail.y < 1.0f )
            return;

        const ::Desert::UI::Rect fit = ::Desert::UI::CanvasRect( static_cast<float>( m_TargetWidth ),
                                             static_cast<float>( m_TargetHeight ), avail.x, avail.y );
        const ImVec2 imageMin( origin.x + fit.X, origin.y + fit.Y );
        const ImVec2 imageMax( imageMin.x + fit.W, imageMin.y + fit.H );

        if ( m_PreviewRecorded )
        {
            // Built on first use rather than in the constructor: the panel is constructed while the editor
            // layer is still being assembled, and the ImGui texture cache needs a live renderer backend.
            if ( !m_UIHelper )
            {
                m_UIHelper = std::make_unique<Editor::UI::UIHelper>();
                m_UIHelper->Init();
            }
            ImGui::SetCursorScreenPos( imageMin );
            m_UIHelper->Image( m_Target->GetColorAttachmentImage(), ImVec2( fit.W, fit.H ) );
        }
        ImGui::GetWindowDrawList()->AddRect( imageMin, imageMax, IM_COL32( 90, 90, 100, 200 ) );

        ImGui::SetCursorScreenPos( origin );
        ImGui::Dummy( avail );
    }

    bool UIEditorPanel::IsRelevant() const
    {
        return SelectionHas<ECS::UILayoutComponent>( m_Scene ) || SelectionHas<ECS::UICanvasComponent>( m_Scene );
    }

} // namespace Desert::Editor

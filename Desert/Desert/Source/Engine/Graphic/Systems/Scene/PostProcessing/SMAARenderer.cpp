#include "SMAARenderer.hpp"
#include "SMAASearchTexData.h" // exact SMAA SearchTex bytes (R8) — engine-owned copy

#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Core/IO/ImageReader.hpp>

namespace Desert::Graphic::System
{
    namespace
    {
        std::shared_ptr<Framebuffer> MakeColorFB( std::string_view name, Core::Formats::ImageFormat format,
                                                  uint32_t w, uint32_t h )
        {
            FramebufferSpecification fbSpec;
            fbSpec.DebugName = std::string( name );
            fbSpec.Attachments.Attachments.push_back( format );
            auto fb = Graphic::Framebuffer::Create( fbSpec );
            fb->Resize( w, h );
            return fb;
        }

        std::shared_ptr<GraphicsPipeline> MakePipeline( std::string_view name,
                                                        const std::shared_ptr<Framebuffer>& fb,
                                                        const std::shared_ptr<Shader>&       shader )
        {
            Graphic::GraphicsPipelineSpecification spec;
            spec.DebugName   = std::string( name );
            spec.Framebuffer = fb;
            spec.Shader      = shader;
            auto pipeline    = Graphic::GraphicsPipeline::Create( spec );
            pipeline->Invalidate();
            return pipeline;
        }
    } // namespace

    Common::BoolResultStr SMAARenderer::Initialize()
    {
        const auto& targetFramebuffer = m_TargetFramebuffer.lock();
        if ( !targetFramebuffer )
        {
            DESERT_VERIFY( false );
        }

        const uint32_t w = targetFramebuffer->GetFramebufferWidth();
        const uint32_t h = targetFramebuffer->GetFramebufferHeight();

        // Intermediate targets: edges + weights are LDR (RGBA8); final matches the FXAA output (RGBA32F).
        m_EdgesFB    = MakeColorFB( "SMAAEdges", Core::Formats::ImageFormat::RGBA8F, w, h );
        m_WeightsFB  = MakeColorFB( "SMAAWeights", Core::Formats::ImageFormat::RGBA8F, w, h );
        m_Framebuffer = MakeColorFB( "SMAABlend", Core::Formats::ImageFormat::RGBA32F, w, h );

        auto& shaders   = *Runtime::ResourceRegistry::GetShaderService();
        m_EdgesShader   = shaders.GetByName( "SMAAEdges" );
        m_WeightsShader = shaders.GetByName( "SMAAWeights" );
        m_BlendShader   = shaders.GetByName( "SMAABlend" );

        m_EdgesPipeline   = MakePipeline( "SMAAEdges", m_EdgesFB, m_EdgesShader );
        m_WeightsPipeline = MakePipeline( "SMAAWeights", m_WeightsFB, m_WeightsShader );
        m_BlendPipeline   = MakePipeline( "SMAABlend", m_Framebuffer, m_BlendShader );

        m_MatEdges   = std::make_unique<MaterialSMAAEdges>();
        m_MatWeights = std::make_unique<MaterialSMAAWeights>();
        m_MatBlend   = std::make_unique<MaterialSMAABlend>();

        LoadLUTs();

        return BOOLSUCCESS;
    }

    void SMAARenderer::LoadLUTs()
    {
        // AreaTex: from PNG (it's a real RGBA8 image — loads exactly via stbi).
        {
            auto img = Core::IO::ImageReader::Read( "Resources/Textures/SMAA/AreaTex.png", true );
            if ( img.Width == 0 || img.Height == 0 )
            {
                LOG_ERROR( "SMAARenderer: failed to load AreaTex.png" );
            }
            else
            {
                Core::Formats::Image2DSpecification spec{
                     .Tag        = "SMAA_AreaTex",
                     .Width      = img.Width,
                     .Height     = img.Height,
                     .Format     = Core::Formats::ImageFormat::RGBA8F,
                     .Mips       = 1,
                     .Data       = std::move( img.Data ),
                     .Usage      = Core::Formats::Image2DUsage::Image2D,
                     .Properties = Core::Formats::ImageProperties::Sample,
                };
                m_AreaTex = Image2D::Create( spec, nullptr );
            }
        }

        // SearchTex: built from the EXACT embedded bytes (the .png is a 2-bit palette that doesn't
        // round-trip reliably, and SMAA's search is very sensitive to it). R8 -> RGBA8 (value in .r).
        {
            std::vector<unsigned char> rgba( static_cast<size_t>( SEARCHTEX_WIDTH ) * SEARCHTEX_HEIGHT * 4, 0 );
            for ( size_t i = 0; i < static_cast<size_t>( SEARCHTEX_WIDTH ) * SEARCHTEX_HEIGHT; ++i )
            {
                rgba[i * 4 + 0] = searchTexBytes[i];
                rgba[i * 4 + 3] = 255;
            }
            Core::Formats::Image2DSpecification spec{
                 .Tag        = "SMAA_SearchTex",
                 .Width      = static_cast<uint32_t>( SEARCHTEX_WIDTH ),
                 .Height     = static_cast<uint32_t>( SEARCHTEX_HEIGHT ),
                 .Format     = Core::Formats::ImageFormat::RGBA8F,
                 .Mips       = 1,
                 .Data       = std::move( rgba ),
                 .Usage      = Core::Formats::Image2DUsage::Image2D,
                 .Properties = Core::Formats::ImageProperties::Sample,
            };
            m_SearchTex = Image2D::Create( spec, nullptr );
        }
    }

    void SMAARenderer::Execute()
    {
        const auto& input = m_TargetFramebuffer.lock();
        if ( !input || !m_AreaTex || !m_SearchTex )
        {
            LOG_ERROR( "SMAARenderer::Execute: missing input framebuffer or LUTs" );
            return;
        }

        auto&      renderer   = Renderer::GetInstance();
        const auto inputColor = input->GetColorAttachmentImage();

        // Pass 1 — edge detection: scene color -> edges.
        {
            auto rp = RenderPass::Create( { .TargetFramebuffer = m_EdgesFB, .DebugName = "SMAAEdgesPass" } );
            renderer.BeginRenderPass( rp.get() );
            m_MatEdges->Bind( inputColor );
            renderer.SubmitFullscreenQuad( m_EdgesPipeline.get(), m_MatEdges->GetMaterialExecutor() );
            renderer.EndRenderPass();
        }

        // Pass 2 — blend weights: edges + AreaTex + SearchTex -> weights.
        {
            auto rp = RenderPass::Create( { .TargetFramebuffer = m_WeightsFB, .DebugName = "SMAAWeightsPass" } );
            renderer.BeginRenderPass( rp.get() );
            m_MatWeights->Bind( m_EdgesFB->GetColorAttachmentImage().get(), m_AreaTex.get(), m_SearchTex.get() );
            renderer.SubmitFullscreenQuad( m_WeightsPipeline.get(), m_MatWeights->GetMaterialExecutor() );
            renderer.EndRenderPass();
        }

        // Pass 3 — neighborhood blending: scene color + weights -> final.
        {
            auto rp = RenderPass::Create( { .TargetFramebuffer = m_Framebuffer, .DebugName = "SMAABlendPass" } );
            renderer.BeginRenderPass( rp.get() );
            m_MatBlend->Bind( inputColor, m_WeightsFB->GetColorAttachmentImage().get(),
                              m_EdgesFB->GetColorAttachmentImage().get(), m_AreaTex.get() );
            renderer.SubmitFullscreenQuad( m_BlendPipeline.get(), m_MatBlend->GetMaterialExecutor() );
            renderer.EndRenderPass();
        }
    }

    void SMAARenderer::Resize( uint32_t width, uint32_t height )
    {
        if ( m_EdgesFB )   m_EdgesFB->Resize( width, height );
        if ( m_WeightsFB ) m_WeightsFB->Resize( width, height );
        if ( m_Framebuffer ) m_Framebuffer->Resize( width, height );
    }
} // namespace Desert::Graphic::System

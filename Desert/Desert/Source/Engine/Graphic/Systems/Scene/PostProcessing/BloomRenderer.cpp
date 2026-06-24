#include "BloomRenderer.hpp"

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic::System
{
    namespace
    {
        constexpr Core::Formats::ImageFormat kBloomFormat = Core::Formats::ImageFormat::RGBA32F;
    }

    Common::BoolResultStr BloomRenderer::Initialize()
    {
        const auto& target = m_TargetFramebuffer.lock();
        if ( !target )
            return Common::MakeError( "BloomRenderer: target framebuffer is not available" );

        if ( !CreateFramebuffers( target->GetFramebufferWidth(), target->GetFramebufferHeight() ) )
            return Common::MakeError( "BloomRenderer: failed to create framebuffers" );

        if ( !CreatePipelines() )
            return Common::MakeError( "BloomRenderer: failed to create pipelines" );

        m_MaterialBright = std::make_unique<MaterialBloomBright>();
        m_BlurMaterials.clear();
        for ( int i = 0; i < kIterations * 2; ++i )
            m_BlurMaterials.push_back( std::make_unique<MaterialBloomBlur>() );

        return BOOLSUCCESS;
    }

    void BloomRenderer::Shutdown()
    {
        m_BrightPipeline.reset();
        m_BlurPipeline.reset();
        m_MaterialBright.reset();
        m_BlurMaterials.clear();
        m_BrightFB.reset();
        m_BlurTemp.reset();
    }

    bool BloomRenderer::CreateFramebuffers( uint32_t width, uint32_t height )
    {
        const auto make = [&]( const std::string& name )
        {
            FramebufferSpecification spec;
            spec.DebugName = name;
            spec.Attachments.Attachments.push_back( kBloomFormat );

            auto framebuffer = Graphic::Framebuffer::Create( spec );
            framebuffer->Resize( width, height );
            return framebuffer;
        };

        m_BrightFB    = make( "Bloom_Bright" );
        m_BlurTemp    = make( "Bloom_BlurA" );
        m_Framebuffer = make( "Bloom_Output" );

        return m_BrightFB && m_BlurTemp && m_Framebuffer;
    }

    bool BloomRenderer::CreatePipelines()
    {
        const auto shaderService = Runtime::ResourceRegistry::GetShaderService();

        const auto makePipeline = [&]( const std::string& shaderName, const std::shared_ptr<Framebuffer>& fb,
                                       const std::string& debugName ) -> std::shared_ptr<GraphicsPipeline>
        {
            const auto shader = shaderService->GetByName( shaderName );
            if ( !shader )
            {
                LOG_ERROR( "BloomRenderer: missing shader '{}'", shaderName );
                return nullptr;
            }

            GraphicsPipelineSpecification spec;
            spec.DebugName         = debugName;
            spec.Shader            = shader;
            spec.Framebuffer       = fb;
            spec.DepthTestEnabled  = false;
            spec.DepthWriteEnabled = false;
            spec.CullMode          = CullMode::None;

            auto pipeline = GraphicsPipeline::Create( spec );
            pipeline->Invalidate();
            return pipeline;
        };

        m_BrightPipeline = makePipeline( "BloomBright", m_BrightFB, "BloomBrightPipeline" );
        m_BlurPipeline   = makePipeline( "BloomBlur", m_BlurTemp, "BloomBlurPipeline" );

        return m_BrightPipeline && m_BlurPipeline;
    }

    void BloomRenderer::Resize( uint32_t width, uint32_t height )
    {
        if ( width == 0 || height == 0 )
            return;

        if ( m_BrightFB )
            m_BrightFB->Resize( width, height );
        if ( m_BlurTemp )
            m_BlurTemp->Resize( width, height );
        if ( m_Framebuffer )
            m_Framebuffer->Resize( width, height );
    }

    void BloomRenderer::RunQuad( const std::shared_ptr<Framebuffer>& target, const std::string& debugName,
                                 const GraphicsPipeline* pipeline, const MaterialExecutor* executor )
    {
        auto& renderer = Renderer::GetInstance();

        auto renderPass = RenderPass::Create( {
             .TargetFramebuffer = target,
             .DebugName         = debugName,
        } );

        renderer.BeginRenderPass( renderPass.get(), true );
        renderer.SubmitFullscreenQuad( pipeline, executor );
        renderer.EndRenderPass();
    }

    void BloomRenderer::Execute()
    {
        const auto& scene = m_TargetFramebuffer.lock();
        if ( !scene )
        {
            LOG_ERROR( "BloomRenderer::Execute: scene framebuffer is unavailable" );
            return;
        }

        // Bright-pass: scene HDR -> bright (threshold extract; emissive objects exceed it and glow).
        m_MaterialBright->Bind( scene->GetColorAttachmentImage().get(), m_Threshold );
        RunQuad( m_BrightFB, "BloomBright", m_BrightPipeline.get(), m_MaterialBright->GetMaterialExecutor() );

        // Separable blur, ping-ponging bright -> temp (H) -> output (V), repeated.
        Image2D* src       = m_BrightFB->GetColorAttachmentImage().get();
        int      materialIx = 0;
        for ( int iter = 0; iter < kIterations; ++iter )
        {
            m_BlurMaterials[materialIx]->Bind( src, glm::vec2( kSpread, 0.0f ) );
            RunQuad( m_BlurTemp, "BloomBlurH", m_BlurPipeline.get(),
                     m_BlurMaterials[materialIx]->GetMaterialExecutor() );
            ++materialIx;

            m_BlurMaterials[materialIx]->Bind( m_BlurTemp->GetColorAttachmentImage().get(),
                                               glm::vec2( 0.0f, kSpread ) );
            RunQuad( m_Framebuffer, "BloomBlurV", m_BlurPipeline.get(),
                     m_BlurMaterials[materialIx]->GetMaterialExecutor() );
            ++materialIx;

            src = m_Framebuffer->GetColorAttachmentImage().get();
        }
    }
} // namespace Desert::Graphic::System

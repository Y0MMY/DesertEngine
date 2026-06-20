#include "JumpFloodOutlineRenderer.hpp"

#include <Engine/Runtime/ResourceRegistry.hpp>

#include <algorithm>
#include <cmath>

namespace Desert::Graphic::System
{
    namespace
    {
        constexpr Core::Formats::ImageFormat kSeedFormat = Core::Formats::ImageFormat::RGBA32F;
    }

    uint32_t JumpFloodOutlineRenderer::ComputeStepCount( uint32_t width, uint32_t height )
    {
        const uint32_t maxDim = std::max( { width, height, 2u } );
        return std::max( 1u, static_cast<uint32_t>( std::ceil( std::log2( static_cast<float>( maxDim ) ) ) ) );
    }

    Common::BoolResultStr JumpFloodOutlineRenderer::Initialize()
    {
        const auto& targetFramebuffer = m_TargetFramebuffer.lock();
        if ( !targetFramebuffer )
        {
            return Common::MakeError( "JumpFloodOutlineRenderer: target framebuffer is not available" );
        }

        const uint32_t width  = targetFramebuffer->GetFramebufferWidth();
        const uint32_t height = targetFramebuffer->GetFramebufferHeight();

        if ( !CreateFramebuffers( width, height ) )
        {
            return Common::MakeError( "JumpFloodOutlineRenderer: failed to create framebuffers" );
        }

        if ( !CreatePipelines() )
        {
            return Common::MakeError( "JumpFloodOutlineRenderer: failed to create pipelines" );
        }

        m_MaterialInit      = std::make_unique<MaterialJFAInit>();
        m_MaterialComposite = std::make_unique<MaterialJFAComposite>();

        m_StepCount = ComputeStepCount( width, height );
        m_StepMaterials.clear();
        for ( uint32_t i = 0; i < m_StepCount; ++i )
        {
            m_StepMaterials.push_back( std::make_unique<MaterialJFAStep>() );
        }

        BindResources();

        return BOOLSUCCESS;
    }

    void JumpFloodOutlineRenderer::SetMaskFramebuffer( const std::weak_ptr<Framebuffer>& maskFramebuffer )
    {
        m_MaskFramebuffer = maskFramebuffer;
        BindResources();
    }

    void JumpFloodOutlineRenderer::BindResources()
    {
        // Step i always reads seed[i % 2] and runs at a fixed sample distance — bind once.
        for ( uint32_t i = 0; i < m_StepCount; ++i )
        {
            const int stepLength = 1 << ( m_StepCount - 1 - i );
            m_StepMaterials[i]->Bind( m_SeedFramebuffers[i % 2]->GetColorAttachmentImage().get(), stepLength );
        }

        // Final reads the last seed buffer (fixed parity) and the scene color.
        if ( const auto& sceneFramebuffer = m_TargetFramebuffer.lock() )
        {
            m_MaterialComposite->SetTextures( m_SeedFramebuffers[m_StepCount % 2]->GetColorAttachmentImage().get(),
                                              sceneFramebuffer->GetColorAttachmentImage().get() );
        }

        // Init reads the silhouette mask (if already wired).
        if ( const auto& maskFramebuffer = m_MaskFramebuffer.lock() )
        {
            m_MaterialInit->Bind( maskFramebuffer->GetColorAttachmentImage().get() );
        }
    }

    void JumpFloodOutlineRenderer::Shutdown()
    {
        m_InitPipeline.reset();
        m_StepPipeline.reset();
        m_FinalPipeline.reset();
        m_MaterialInit.reset();
        m_StepMaterials.clear();
        m_MaterialComposite.reset();
        m_SeedFramebuffers[0].reset();
        m_SeedFramebuffers[1].reset();
    }

    bool JumpFloodOutlineRenderer::CreateFramebuffers( uint32_t width, uint32_t height )
    {
        const auto makeFramebuffer = [&]( const std::string& name )
        {
            FramebufferSpecification spec;
            spec.DebugName = name;
            spec.Attachments.Attachments.push_back( kSeedFormat );

            auto framebuffer = Graphic::Framebuffer::Create( spec );
            framebuffer->Resize( width, height );
            return framebuffer;
        };

        m_SeedFramebuffers[0] = makeFramebuffer( "JFA_Seed0" );
        m_SeedFramebuffers[1] = makeFramebuffer( "JFA_Seed1" );
        m_Framebuffer         = makeFramebuffer( "JFA_Output" );

        return m_SeedFramebuffers[0] && m_SeedFramebuffers[1] && m_Framebuffer;
    }

    bool JumpFloodOutlineRenderer::CreatePipelines()
    {
        const auto shaderService = Runtime::ResourceRegistry::GetShaderService();

        const auto makePipeline = [&]( const std::string& shaderName, const std::shared_ptr<Framebuffer>& framebuffer,
                                       const std::string& debugName ) -> std::shared_ptr<GraphicsPipeline>
        {
            const auto shader = shaderService->GetByName( shaderName );
            if ( !shader )
            {
                LOG_ERROR( "JumpFloodOutlineRenderer: missing shader '{}'", shaderName );
                return nullptr;
            }

            GraphicsPipelineSpecification spec;
            spec.DebugName         = debugName;
            spec.Shader            = shader;
            spec.Framebuffer       = framebuffer;
            spec.DepthTestEnabled  = false;
            spec.DepthWriteEnabled = false;
            spec.CullMode          = CullMode::None;

            auto pipeline = GraphicsPipeline::Create( spec );
            pipeline->Invalidate();
            return pipeline;
        };

        m_InitPipeline  = makePipeline( "JFA_Init", m_SeedFramebuffers[0], "JFA_InitPipeline" );
        m_StepPipeline  = makePipeline( "JFA_Step", m_SeedFramebuffers[0], "JFA_StepPipeline" );
        m_FinalPipeline = makePipeline( "JFA_Final", m_Framebuffer, "JFA_FinalPipeline" );

        return m_InitPipeline && m_StepPipeline && m_FinalPipeline;
    }

    void JumpFloodOutlineRenderer::OnResize( uint32_t width, uint32_t height )
    {
        if ( width == 0 || height == 0 )
            return;

        if ( m_SeedFramebuffers[0] )
            m_SeedFramebuffers[0]->Resize( width, height );
        if ( m_SeedFramebuffers[1] )
            m_SeedFramebuffers[1]->Resize( width, height );
        if ( m_Framebuffer )
            m_Framebuffer->Resize( width, height );

        const uint32_t newStepCount = ComputeStepCount( width, height );
        if ( newStepCount != m_StepCount )
        {
            m_StepCount = newStepCount;
            m_StepMaterials.clear();
            for ( uint32_t i = 0; i < m_StepCount; ++i )
            {
                m_StepMaterials.push_back( std::make_unique<MaterialJFAStep>() );
            }
        }

        // Framebuffer images were recreated; rebind every material's input textures.
        BindResources();
    }

    void JumpFloodOutlineRenderer::RunQuad( const std::shared_ptr<Framebuffer>& target, const std::string& debugName,
                                            const GraphicsPipeline* pipeline, const MaterialExecutor* executor )
    {
        auto& renderer = Renderer::GetInstance();

        auto renderPass = RenderPass::Create( {
             .TargetFramebuffer = target,
             .DebugName         = debugName,
        } );

        renderer.BeginRenderPass( renderPass.get() );
        renderer.SubmitFullscreenQuad( pipeline, executor );
        renderer.EndRenderPass();
    }

    void JumpFloodOutlineRenderer::Execute()
    {
        const auto& sceneFramebuffer = m_TargetFramebuffer.lock();
        if ( !sceneFramebuffer )
        {
            LOG_ERROR( "JumpFloodOutlineRenderer::Execute: scene framebuffer is unavailable" );
            return;
        }

        // Index of the seed framebuffer holding the most recent result. Step i writes seed[(i+1)%2],
        // so after m_StepCount steps the result lives in seed[m_StepCount % 2] (a fixed parity).
        if ( m_Enabled && !m_MaskFramebuffer.expired() )
        {
            // Init: silhouette mask -> seed[0]. (Input textures were bound once in BindResources.)
            RunQuad( m_SeedFramebuffers[0], "JFA_Init", m_InitPipeline.get(),
                     m_MaterialInit->GetMaterialExecutor() );

            // Ping-pong propagation steps with halving sample distance.
            int readIndex = 0;
            for ( uint32_t i = 0; i < m_StepCount; ++i )
            {
                const int writeIndex = 1 - readIndex;
                RunQuad( m_SeedFramebuffers[writeIndex], "JFA_Step", m_StepPipeline.get(),
                         m_StepMaterials[i]->GetMaterialExecutor() );
                readIndex = writeIndex;
            }
        }

        // Final composite -> output framebuffer. Only the outline parameters change per frame; the
        // input textures stay bound from BindResources. When disabled, width 0 makes the shader pass
        // the scene through unchanged (JFA_Final early-out).
        const float effectiveWidth = m_Enabled ? m_OutlineWidth : 0.0f;
        if ( m_OutlineColor != m_AppliedColor || effectiveWidth != m_AppliedWidth ||
             m_Smoothness != m_AppliedSmoothness )
        {
            m_MaterialComposite->SetParams( { m_OutlineColor, effectiveWidth, m_Smoothness } );
            m_AppliedColor      = m_OutlineColor;
            m_AppliedWidth      = effectiveWidth;
            m_AppliedSmoothness = m_Smoothness;
        }
        RunQuad( m_Framebuffer, "JFA_Final", m_FinalPipeline.get(), m_MaterialComposite->GetMaterialExecutor() );
    }
} // namespace Desert::Graphic::System

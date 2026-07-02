#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/Deferred/MaterialCopy.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic::System
{
    // Full-screen copy: snapshots a source image into this system's target framebuffer. Used to copy the
    // composited scene colour into a separate texture the glass pass samples (screen-space refraction), so the
    // glass pass never reads + writes the same attachment (feedback loop).
    class CopyRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override
        {
            m_Shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "Copy" );
            if ( !m_Shader )
                return Common::MakeError( "Copy shader not found" );

            const auto& target = m_TargetFramebuffer.lock();
            if ( !target )
                return Common::MakeError( "Copy target framebuffer missing" );

            GraphicsPipelineSpecification spec;
            spec.DebugName         = "Copy";
            spec.Framebuffer       = target;
            spec.Shader            = m_Shader;
            spec.DepthTestEnabled  = false;
            spec.DepthWriteEnabled = false;
            m_Pipeline             = Graphic::GraphicsPipeline::Create( spec );
            m_Pipeline->Invalidate();

            m_Material = std::make_unique<MaterialCopy>();
            return BOOLSUCCESS;
        }

        virtual void Shutdown() override
        {
        }

        void RegisterPasses( RenderGraphBuilder& ) override
        {
        }

        void Execute( const std::shared_ptr<Image2D>& src )
        {
            const auto& target = m_TargetFramebuffer.lock();
            if ( !target || !src || !m_Pipeline || !m_Material )
                return;

            auto renderPass = RenderPass::Create( {
                 .TargetFramebuffer = target,
                 .DebugName         = "SceneColorCopyPass",
            } );

            auto& renderer = Renderer::GetInstance();
            renderer.BeginRenderPass( renderPass.get() ); // clear + write the snapshot
            m_Material->Bind( src );
            renderer.SubmitFullscreenQuad( m_Pipeline.get(), m_Material->GetMaterialExecutor() );
            renderer.EndRenderPass();
        }

        std::shared_ptr<Image2D> GetImage() const
        {
            const auto& target = m_TargetFramebuffer.lock();
            return target ? target->GetColorAttachmentImage( 0 ) : nullptr;
        }

    private:
        std::shared_ptr<Shader>           m_Shader;
        std::shared_ptr<GraphicsPipeline> m_Pipeline;
        std::unique_ptr<MaterialCopy>     m_Material;
    };
} // namespace Desert::Graphic::System

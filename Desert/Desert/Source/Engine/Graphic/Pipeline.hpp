#pragma once

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/Vertexbuffer.hpp>

namespace Desert::Graphic
{
    struct PipelineSpecification
    {
        std::shared_ptr<Shader>      Shader;
        std::shared_ptr<Framebuffer> Framebuffer;
        std::shared_ptr<RenderPass>  Renderpass;
        VertexBufferLayout           Layout;

        std::string DebugName;
    };

    class Pipeline
    {
    public:
        virtual ~Pipeline() = default;

        virtual const PipelineSpecification GetSpecification() const = 0;

        virtual void Invalidate() = 0;

        static std::shared_ptr<Pipeline> Create( const PipelineSpecification& spec );
    };

    class PipelineCompute
    {
    public:
        virtual ~PipelineCompute() = default;

        virtual void Begin()                                                                      = 0;
        virtual void Execute( uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ )  = 0;
        virtual void Dispatch( uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ ) = 0;
        virtual void End()                                                                        = 0;

        virtual void Invalidate() = 0;
        virtual void Release() = 0;

        static std::shared_ptr<PipelineCompute> Create( const std::shared_ptr<Shader>& shader );
    };

} // namespace Desert::Graphic
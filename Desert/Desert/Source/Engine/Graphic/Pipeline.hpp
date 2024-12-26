#pragma once

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/Vertexbuffer.hpp>

namespace Desert::Graphic
{
    struct PipelineSpecification
    {
        std::shared_ptr<Shader>      Shader;
        std::shared_ptr<Framebuffer> Framebuffer;
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
} // namespace Desert::Graphic

#pragma once

#include <Engine/Graphic/Framebuffer.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    struct RenderPassSpecification
    {
        struct
        {
            glm::vec4 Color        = { 0.0f, 0.0f, 0.0f, 1.0f }; 
            glm::vec2 DepthStencil = { 1.0f, 0 }; // X = Depth, Y = Stencil
        } ClearColor;

        std::shared_ptr<Framebuffer> TargetFramebuffer;
        std::string                  DebugName;
    };
    class RenderPass final
    {
    public:
        virtual ~RenderPass() = default;

        RenderPass( const RenderPassSpecification& spec );

        virtual RenderPassSpecification& GetSpecification()
        {
            return m_RenderPassSpecification;
        }
        virtual const RenderPassSpecification& GetSpecification() const
        {
            return m_RenderPassSpecification;
        }

        static std::shared_ptr<RenderPass> Create( const RenderPassSpecification& spec );

    private:
        RenderPassSpecification m_RenderPassSpecification;
    };
} // namespace Desert::Graphic
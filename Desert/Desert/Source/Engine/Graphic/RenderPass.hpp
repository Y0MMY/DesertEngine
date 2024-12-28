
#pragma once

#include <Engine/Graphic/Framebuffer.hpp>

namespace Desert::Graphic
{
    struct RenderPassSpecification
    {
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
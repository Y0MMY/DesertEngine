#include <Engine/Graphic/RenderPass.hpp>

namespace Desert::Graphic
{

    std::shared_ptr<RenderPass> RenderPass::Create( const RenderPassSpecification& spec )
    {
        return std::make_shared<RenderPass>( spec );
    }

    RenderPass::RenderPass( const RenderPassSpecification& spec ) : m_RenderPassSpecification( spec )
    {
    }

} // namespace Desert::Graphic
#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

#include <Engine/Core/Window.hpp>

#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Geometry/Mesh.hpp>

namespace Desert::Graphic
{
    enum class RendererAPIType : uint8_t
    {
        None   = 0,
        Vulkan = 1,
    };

    class RendererAPI
    {
    public:
        explicit RendererAPI( const std::shared_ptr<Window>& window ) : m_Window( window )
        {
        }
        virtual ~RendererAPI() = default;

    public:
        virtual void Init()     = 0;
        virtual void Shutdown() = 0;

        virtual Common::BoolResultStr BeginFrame()                                                = 0;
        virtual Common::BoolResultStr EndFrame()                                                  = 0;
        virtual Common::BoolResultStr PrepareNextFrame()                                          = 0;
        virtual Common::BoolResultStr PresentFinalImage()                                         = 0;
        virtual Common::BoolResultStr BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass,
                                                    bool                               clearFrame )                          = 0;
        virtual Common::BoolResultStr BeginSwapChainRenderPass()                                  = 0;
        virtual Common::BoolResultStr EndRenderPass()                                             = 0;
        virtual void RenderMesh( const std::shared_ptr<Pipeline>& pipeline, const std::shared_ptr<Mesh>& mesh,
                                 const std::shared_ptr<MaterialExecutor>& material )           = 0;
        virtual void SubmitFullscreenQuad( const std::shared_ptr<Pipeline>&         pipeline,
                                           const std::shared_ptr<MaterialExecutor>& material ) = 0;

        virtual void                         ResizeWindowEvent( uint32_t width, uint32_t height ) = 0;
        virtual std::shared_ptr<Framebuffer> GetCompositeFramebuffer() const                      = 0;

    public:
        static const RendererAPIType GetAPIType()
        {
            return s_RenderingAPI;
        }

    protected:
        std::weak_ptr<Window>         m_Window;
        static inline RendererAPIType s_RenderingAPI = RendererAPIType::Vulkan;
    };

} // namespace Desert::Graphic
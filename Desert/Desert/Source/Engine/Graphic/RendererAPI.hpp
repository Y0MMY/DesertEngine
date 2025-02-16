#pragma once

#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>
#include <Engine/Graphic/IndexBuffer.hpp>
#include <Engine/Graphic/Pipeline.hpp>

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
        virtual ~RendererAPI() = default;

    public:
        virtual void Init() = 0;

        virtual Common::BoolResult BeginFrame()                                                     = 0;
        virtual Common::BoolResult EndFrame()                                                       = 0;
        virtual Common::BoolResult PresentFinalImage()                                              = 0;
        virtual Common::BoolResult BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass ) = 0;
        virtual Common::BoolResult EndRenderPass()                                                  = 0;

        virtual void TEST_DrawTriangle( const std::shared_ptr<VertexBuffer>& vertexBuffer,
                                        const std::shared_ptr<IndexBuffer>& indexBuffer,
                                        const std::shared_ptr<Pipeline>&     pipeline ) = 0; // TEMP
    public:
        static const RendererAPIType GetAPIType()
        {
            return s_RenderingAPI;
        }

    private:
        static inline RendererAPIType s_RenderingAPI = RendererAPIType::Vulkan;
    };

} // namespace Desert::Graphic
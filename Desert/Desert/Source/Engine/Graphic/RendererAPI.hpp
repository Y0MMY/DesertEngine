#pragma once

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
        virtual void Init()       = 0;
        virtual void ClearImage() = 0;

        virtual void     BeginFrame()           = 0;
        virtual void EndFrame() = 0;

    public:
        static const RendererAPIType GetAPIType()
        {
            return s_RenderingAPI;
        }

    private:
        static inline RendererAPIType s_RenderingAPI = RendererAPIType::Vulkan;
    };

} // namespace Desert::Graphic
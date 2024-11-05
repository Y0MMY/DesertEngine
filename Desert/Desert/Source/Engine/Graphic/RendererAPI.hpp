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
    public:
        static const RendererAPIType GetAPIType()
        {
            return s_RenderingAPI;
        }

    private:
        static inline RendererAPIType s_RenderingAPI = RendererAPIType::Vulkan;
    };

} // namespace Desert::Graphic
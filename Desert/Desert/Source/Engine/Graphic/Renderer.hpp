#pragma once

#include <Engine/Graphic/RendererContext.hpp>

namespace Desert::Graphic
{
    class Renderer : public Common::Singleton<Renderer>
    {
    public:
        Common::BoolResult Init();

        const auto& GetRendererContext() const
        {
            return m_RendererContext;
        }

        void BeginFrame();
        void EndFrame();

        void ClearImage();


        uint32_t GetCurrentFrameIndex();
    private:
        void InitGraphicAPI();

    private:
        std::shared_ptr<RendererContext> m_RendererContext;
    };
} // namespace Desert::Graphic
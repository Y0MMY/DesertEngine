#pragma once

#include <GLFW/glfw3.h>

namespace Desert::Graphic
{
    class RendererContext
    {
    public:
        virtual ~RendererContext() = default;

        virtual void BeginFrame() const = 0;
        virtual void EndFrame() const   = 0;

        virtual void OnResize( uint32_t width, uint32_t height ) = 0;
        virtual void Shutdown()                                  = 0;

        static std::unique_ptr<RendererContext> Create( GLFWwindow* window );
    };
} // namespace Desert::Graphic
#pragma once

#include <GLFW/glfw3.h>
#include <Engine/Core/Window.hpp>

namespace Desert
{
    class Window;
}
namespace Desert::Graphic
{
    class RendererContext
    {
    public:
        virtual ~RendererContext() = default;

        virtual void Init()             = 0;
        virtual void BeginFrame() const = 0;
        virtual void EndFrame() const   = 0;

        virtual void OnResize( uint32_t width, uint32_t height ) = 0;
        virtual void Shutdown()                                  = 0;

        static std::shared_ptr<RendererContext> Create( const std::shared_ptr<Window>& window );
    };
} // namespace Desert::Graphic
#include "Window.hpp"
#if defined( DESERT_PLATFORM_WINDOWS )
#include <Platform/Windows/WindowsWindow.hpp>
#endif

namespace Desert
{
    std::shared_ptr<Window> Window::Create( const WindowSpecification& specification )
    {
#if defined( DESERT_PLATFORM_WINDOWS )
        return std::make_shared<Desert::Platform::Windows::WindowsWindow>( specification );
#endif
    }

} // namespace Common
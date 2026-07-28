#include "Window.hpp"
#if defined( DESERT_PLATFORM_WINDOWS )
#include <Platform/Windows/WindowsWindow.hpp>
#elif defined( DESERT_PLATFORM_MACOS )
#include <Platform/MacOS/MacOSWindow.hpp>
#endif

namespace Desert
{
    std::shared_ptr<Window> Window::Create( const WindowSpecification& specification )
    {
#if defined( DESERT_PLATFORM_WINDOWS )
        return std::make_shared<Desert::Platform::Windows::WindowsWindow>( specification );
#elif defined( DESERT_PLATFORM_MACOS )
        return std::make_shared<Desert::Platform::MacOS::MacOSWindow>( specification );
#else
#error "Window::Create: unsupported platform"
#endif
    }

} // namespace Common

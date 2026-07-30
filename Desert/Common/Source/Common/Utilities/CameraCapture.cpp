#include "CameraCapture.hpp"

#if defined( DESERT_PLATFORM_MACOS )
#include <Common/Platform/MacOS/MacOSCamera.hpp>
#endif

namespace Common::Utils
{
    CameraCapture::CameraCapture()
    {
#if defined( DESERT_PLATFORM_MACOS )
        m_Impl = Mac::CameraCreate();
#endif
    }

    CameraCapture::~CameraCapture()
    {
#if defined( DESERT_PLATFORM_MACOS )
        if ( m_Impl )
            Mac::CameraDestroy( m_Impl );
#endif
        m_Impl = nullptr;
    }

    bool CameraCapture::Start()
    {
#if defined( DESERT_PLATFORM_MACOS )
        return m_Impl && Mac::CameraStart( m_Impl );
#else
        return false;
#endif
    }

    void CameraCapture::Stop()
    {
#if defined( DESERT_PLATFORM_MACOS )
        if ( m_Impl )
            Mac::CameraStop( m_Impl );
#endif
    }

    bool CameraCapture::IsRunning() const
    {
#if defined( DESERT_PLATFORM_MACOS )
        return m_Impl && Mac::CameraIsRunning( m_Impl );
#else
        return false;
#endif
    }

    bool CameraCapture::GetLatestFrame( std::vector<uint8_t>& out, int& width, int& height )
    {
#if defined( DESERT_PLATFORM_MACOS )
        return m_Impl && Mac::CameraGetFrame( m_Impl, out, width, height );
#else
        (void)out;
        (void)width;
        (void)height;
        return false;
#endif
    }
} // namespace Common::Utils

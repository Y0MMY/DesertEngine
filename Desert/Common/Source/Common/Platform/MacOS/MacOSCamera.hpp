#pragma once

#include <cstdint>
#include <vector>

// macOS AVFoundation webcam backend for Common::Utils::CameraCapture. These are plain C++ functions (not
// extern "C") so the std::vector frame handoff keeps its C++ ABI across the .cpp <-> .mm boundary (both are
// compiled by the same toolchain). The opaque void* is an AVFoundation session wrapper.
namespace Common::Utils::Mac
{
    void* CameraCreate();
    void  CameraDestroy( void* handle );
    bool  CameraStart( void* handle );
    void  CameraStop( void* handle );
    bool  CameraIsRunning( void* handle );

    // Copies the newest frame into `out` (RGBA8, width*height*4). Returns true only when a new frame arrived
    // since the previous call.
    bool CameraGetFrame( void* handle, std::vector<uint8_t>& out, int& width, int& height );
} // namespace Common::Utils::Mac

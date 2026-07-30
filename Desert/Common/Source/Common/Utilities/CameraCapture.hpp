#pragma once

#include <cstdint>
#include <vector>

namespace Common::Utils
{
    // Live webcam capture. Platform-backed (macOS AVFoundation; stub elsewhere). Frames are delivered on a
    // background thread by the OS; GetLatestFrame() hands the newest one to the caller as a tight RGBA8
    // buffer (width*height*4). Construct, Start(), poll GetLatestFrame() each UI frame, Stop() / destroy.
    class CameraCapture
    {
    public:
        CameraCapture();
        ~CameraCapture();

        CameraCapture( const CameraCapture& )            = delete;
        CameraCapture& operator=( const CameraCapture& ) = delete;

        // Opens the default video device and starts the session. Returns false if capture is unavailable
        // (no device / permission denied / unsupported platform). May prompt for camera permission once.
        bool Start();
        void Stop();
        bool IsRunning() const;

        // Copies the most recent frame into `out` (resized to width*height*4, RGBA8) and reports its size.
        // Returns true only when a NEW frame arrived since the last call (so the caller can skip re-uploads).
        bool GetLatestFrame( std::vector<uint8_t>& out, int& width, int& height );

    private:
        void* m_Impl = nullptr; // platform handle (macOS: AVFoundation session wrapper)
    };
} // namespace Common::Utils

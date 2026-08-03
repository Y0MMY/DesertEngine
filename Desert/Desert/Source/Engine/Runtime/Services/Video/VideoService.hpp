#pragma once

#include <Engine/Graphic/Texture.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct plm_t; // pl_mpeg decoder (opaque here; the .cpp includes the implementation header)

namespace Desert::Graphic
{
    class Image2D;
}

namespace Desert::Runtime
{
    // One open video: its pl_mpeg decoder plus a STABLE GPU texture whose pixels are re-uploaded in place
    // each frame (Image2D::SetData) — so the Image2D* the UI walk samples never changes.
    struct VideoPlayback
    {
        plm_t*                                Plm = nullptr;
        std::shared_ptr<Graphic::Texture2D>   Texture;
        std::vector<uint8_t>                  Bytes; // source file kept alive: pl_mpeg demuxes it in place
        std::vector<uint8_t>                  Rgba;  // latest decoded frame, tightly-packed RGBA8
        int                                   Width  = 0;
        int                                   Height = 0;
        bool                                  Dirty  = false; // a new frame decoded since the last upload
        bool                                  Valid  = false; // false => open failed (negative cache)
        std::chrono::steady_clock::time_point Last{};         // wall-clock of the previous UpdateAll advance
    };

    // Streams MPEG1 videos as UI content. A video opens once on first Resolve (decoder + stable texture);
    // UpdateAll() advances every open video by real elapsed wall-clock time and uploads its newest frame.
    // UpdateAll runs in the host update step (outside the render pass) so the render walk only ever samples
    // an already-updated texture. Playback loops. Audio is disabled for now (video-only; sync is a follow-up).
    class VideoService
    {
    public:
        ~VideoService();

        // GPU image for `path`'s current frame, opening the video on first use. nullptr when the file
        // can't be opened / decoded (negatively cached so the check stays cheap).
        Graphic::Image2D* Resolve( const std::string& path );

        // Advance every open video by real elapsed time and upload its newest frame. Call once per frame
        // from the host update loop (runtime layer + editor).
        void UpdateAll();

        void Clear();

    private:
        VideoPlayback* GetOrOpen( const std::string& path );

        std::unordered_map<std::string, VideoPlayback> m_Videos;
    };
} // namespace Desert::Runtime

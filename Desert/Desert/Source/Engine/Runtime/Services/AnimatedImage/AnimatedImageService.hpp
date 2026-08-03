#pragma once

#include <Engine/Assets/Common.hpp>
#include <Engine/Graphic/Texture.hpp>

#include <chrono>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Desert::Runtime
{
    // Plays animated-image assets (GIF) as UI content. A GIF asset is decoded once on first Resolve — all
    // frames become GPU textures and their durations are baked into a cumulative timeline. The current
    // frame is then a pure function of wall-clock time, so playback is identical in the runtime and the
    // editor authoring viewport with no per-frame tick, dt plumbing, or double-advance across viewports.
    //
    // A GIF still imports through the ordinary texture path (its first frame shows statically anywhere a
    // Sprite handle is used); this service only adds the animation on top when the source is a multi-frame
    // .gif. Non-animated handles are negatively cached so the check stays cheap.
    class AnimatedImageService
    {
    public:
        // The GPU image for `handle`'s current animation frame, or nullptr when the asset isn't a
        // multi-frame GIF (the caller then falls back to the static texture path). Decodes + uploads all
        // frames on first use.
        Graphic::Image2D* Resolve( const Assets::AssetHandle& handle );

        void Clear();

    private:
        struct Anim
        {
            std::vector<std::shared_ptr<Graphic::Texture2D>> Frames;
            std::vector<float> CumEndMs;         // cumulative end time; back() == loop length
            bool               Animated = false; // false => not a multi-frame GIF
        };

        const Anim& GetOrDecode( const Assets::AssetHandle& handle );

        std::unordered_map<Assets::AssetHandle, Anim> m_Anims;
        std::chrono::steady_clock::time_point         m_Epoch = std::chrono::steady_clock::now();
    };
} // namespace Desert::Runtime

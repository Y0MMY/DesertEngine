#pragma once

#include <atomic>

namespace Desert::Graphic
{
    // Low-level render config the Vulkan backend can read without depending on Core/SceneSettings. Pushed
    // from SceneRenderer::BeginScene each frame; the sampler-creation path reads TextureFilter.
    enum class TextureFilterMode : int
    {
        Nearest     = 0, // nearest min/mag + nearest mip
        Bilinear    = 1, // linear min/mag + nearest mip
        Trilinear   = 2, // linear min/mag + linear mip
        Anisotropic = 3, // trilinear + anisotropic filtering (if the device supports it)
    };

    struct RenderConfig
    {
        // Current global texture sampler filter (VulkanImage::CreateSampler reads this).
        static inline std::atomic<int> TextureFilter{ static_cast<int>( TextureFilterMode::Trilinear ) };
        // Device max sampler anisotropy (0 = anisotropy unsupported). Set once at device init.
        static inline std::atomic<float> MaxAnisotropy{ 0.0f };
        // User-selected anisotropy level (1/2/4/8/16); clamped to MaxAnisotropy at sampler creation.
        static inline std::atomic<int> AnisotropyLevel{ 8 };

        // MSAA sample count for the scene's composite framebuffer (1 = off, 2/4/8). Read ONCE at
        // SceneRenderer::Init (pipelines bake their sample count), so a change applies on the next
        // editor start. Clamped to MaxMSAASamples (device color&depth sample caps, set at init).
        static inline std::atomic<int> MSAASamples{ 1 };
        static inline std::atomic<int> MaxMSAASamples{ 1 };
        // What the running renderer actually baked at Init (the UI compares against this to show
        // its "restart to apply" note; MSAASamples may already hold the NEXT start's selection).
        static inline std::atomic<int> MSAASamplesActive{ 1 };
    };
} // namespace Desert::Graphic

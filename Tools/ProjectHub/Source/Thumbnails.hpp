#pragma once

// Project and template pictures: decoded from disk, uploaded once, and re-decoded only when the
// file underneath actually changes.
//
// TWO things make this more than a map. First, the BUDGET: a 512x288 PNG costs 1-3 ms to decode,
// and a browser showing fourteen projects would spend forty of them in the frame that first shows
// the grid — a visible freeze, on the one screen a launcher exists to draw. So at most
// kDecodesPerFrame files are decoded per frame and the rest of the tiles draw their placeholder for
// a frame or two, which nobody can see. Second, the KEY is path + modification time, so an Editor
// that rewrites `<project>/.thumbnail.png` on save shows its new picture the next time the launcher
// looks, without the launcher having to be told.
//
// NO GRAPHICS API APPEARS IN THIS HEADER. The one thing a picture needs from the backend — turn
// these RGBA bytes into something ImGui can draw, and later let it go — arrives as two callbacks.
// The launcher runs on OpenGL2 today and moves to Vulkan with the repository (L3); that is a change
// of the twenty lines in Main.cpp that fill this struct in, and nothing here.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace Hub
{
    // What ImGui draws with. `void*` because that is exactly what ImTextureID is, without dragging
    // imgui.h into every consumer of this header (including the test binary, which has no window).
    using TextureHandle = void*;

    struct TextureBackend
    {
        // RGBA8, tightly packed, `width * height * 4` bytes. Returns nullptr if the upload failed —
        // the cache then remembers the failure rather than retrying it every frame forever.
        std::function<TextureHandle( const std::uint8_t* rgba, int width, int height )> Upload;
        std::function<void( TextureHandle )>                                            Destroy;
    };

    class ThumbnailCache
    {
    public:
        // A 512x288 PNG decodes in 1-3 ms. Two is what fits in a frame beside everything else; the
        // number is here, named, because it is a budget and not an implementation detail.
        static constexpr int kDecodesPerFrame = 2;

        struct Texture
        {
            TextureHandle Id     = nullptr;
            int           Width  = 0;
            int           Height = 0;

            [[nodiscard]] bool Ok() const
            {
                return Id != nullptr;
            }
        };

        ~ThumbnailCache();

        void SetBackend( TextureBackend backend );

        // Call once per frame, before any Get. Refills the decode budget.
        void BeginFrame();

        // The texture for `path` if it is ready, an empty Texture if it is not — in which case a
        // decode is attempted now, budget permitting, and the caller draws its placeholder. `path`
        // empty (no file on disk) is an empty Texture and no work at all.
        [[nodiscard]] Texture Get( const std::string& path );

        // Releases every texture through the backend. Must be called while the graphics device is
        // still alive — a destructor that runs at process exit is not that moment.
        void Shutdown();

        // How many files this cache has decoded since it was created. Exists so a test can assert
        // the BUDGET is a budget: without it "at most two per frame" is a comment.
        [[nodiscard]] int DecodeCount() const
        {
            return m_DecodeCount;
        }

    private:
        struct Entry
        {
            Texture       Image;
            std::uint64_t ModifiedAt = 0;
            // A file that could not be decoded is remembered as failed. Retrying a corrupt PNG on
            // every frame would spend the whole budget on the one tile that can never fill.
            bool Failed = false;
        };

        TextureBackend                         m_Backend;
        std::unordered_map<std::string, Entry> m_Entries;
        int                                    m_Budget      = kDecodesPerFrame;
        int                                    m_DecodeCount = 0;
    };
} // namespace Hub

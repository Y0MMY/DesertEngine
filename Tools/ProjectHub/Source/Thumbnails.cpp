#include "Thumbnails.hpp"

#include <stb_image/stb_image.h>

#include <filesystem>
#include <utility>

namespace Hub
{
    namespace fs = std::filesystem;

    namespace
    {
        // The modification time as a plain integer, or 0 when the file is not there. Half of the
        // cache key: the Editor rewrites `<project>/.thumbnail.png` on every save, and a cache
        // keyed on the path alone would keep showing the picture from the first time it looked.
        std::uint64_t ModifiedAt( const std::string& path )
        {
            std::error_code ec;
            const auto      when = fs::last_write_time( path, ec );
            if ( ec )
                return 0;
            return static_cast<std::uint64_t>( when.time_since_epoch().count() );
        }
    } // namespace

    ThumbnailCache::~ThumbnailCache()
    {
        // Deliberately NOT releasing here. A destructor that runs at process exit runs after the
        // graphics device is gone, and handing a dead device a texture to free is a crash on the
        // way out. Shutdown() is the moment; this is only the reminder.
        m_Entries.clear();
    }

    void ThumbnailCache::SetBackend( TextureBackend backend )
    {
        m_Backend = std::move( backend );
    }

    void ThumbnailCache::BeginFrame()
    {
        m_Budget = kDecodesPerFrame;
    }

    ThumbnailCache::Texture ThumbnailCache::Get( const std::string& path )
    {
        if ( path.empty() || !m_Backend.Upload )
            return {};

        const std::uint64_t modified = ModifiedAt( path );

        if ( const auto found = m_Entries.find( path ); found != m_Entries.end() )
        {
            if ( found->second.ModifiedAt == modified )
                return found->second.Failed ? Texture{} : found->second.Image;

            // The file changed under us. Drop what we hold and fall through to a fresh decode —
            // which still has to pay the budget, so a project saved while the launcher is open
            // updates on the next frame or the one after, not never.
            if ( found->second.Image.Ok() && m_Backend.Destroy )
                m_Backend.Destroy( found->second.Image.Id );
            m_Entries.erase( found );
        }

        if ( m_Budget <= 0 )
            return {}; // out of budget this frame; the tile draws its placeholder and asks again

        --m_Budget;
        ++m_DecodeCount;

        Entry entry;
        entry.ModifiedAt = modified;

        int            width    = 0;
        int            height   = 0;
        int            channels = 0;
        unsigned char* pixels   = stbi_load( path.c_str(), &width, &height, &channels, 4 );
        if ( !pixels || width <= 0 || height <= 0 )
        {
            // Remembered as failed rather than retried: a corrupt PNG would otherwise consume the
            // whole per-frame budget forever and starve every tile that could have loaded.
            if ( pixels )
                stbi_image_free( pixels );
            entry.Failed = true;
            m_Entries.emplace( path, entry );
            return {};
        }

        entry.Image.Id     = m_Backend.Upload( pixels, width, height );
        entry.Image.Width  = width;
        entry.Image.Height = height;
        stbi_image_free( pixels );

        if ( !entry.Image.Ok() )
            entry.Failed = true;
        m_Entries.emplace( path, entry );
        return entry.Failed ? Texture{} : entry.Image;
    }

    void ThumbnailCache::Shutdown()
    {
        if ( m_Backend.Destroy )
            for ( auto& [path, entry] : m_Entries )
                if ( entry.Image.Ok() )
                    m_Backend.Destroy( entry.Image.Id );
        m_Entries.clear();
    }
} // namespace Hub

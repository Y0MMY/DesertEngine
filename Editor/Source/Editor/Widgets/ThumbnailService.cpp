#include "ThumbnailService.hpp"

#include <Editor/Widgets/ThumbnailCache.hpp>

#include <Common/Core/Logger.hpp>

#include <filesystem>

namespace Desert::Editor
{
    namespace
    {
        // A capture takes two frames (render, then read back). If a request has not produced its PNG well
        // past that, something is wrong with the asset — a .demat that fails to parse, a mesh the service
        // never registers — and retrying it forever would keep the queue permanently busy. Generous enough
        // that a slow first-time shader compile is not mistaken for a failure.
        constexpr int kInFlightGiveUpTicks = 240;
    } // namespace

    ThumbnailService& ThumbnailService::Get()
    {
        static ThumbnailService s_Instance;
        return s_Instance;
    }

    bool ThumbnailService::ShouldQueue( const std::string& assetPath, const std::string& png )
    {
        if ( assetPath.empty() )
            return false;
        if ( m_Failed.count( assetPath ) || m_Queued.count( assetPath ) )
            return false;

        // Already captured in a previous session: the on-disk PNG IS the cache, so nothing to do. Staleness
        // (asset edited after the PNG was written) is the caller's call via Invalidate() — the asset browser
        // already makes that comparison and knows its own tolerances.
        std::error_code ec;
        if ( std::filesystem::exists( png, ec ) )
            return false;

        return true;
    }

    std::string ThumbnailService::RequestMaterial( const Assets::AssetHandle& material,
                                                   const std::string& assetPath, bool flatPreview )
    {
        const std::string png = ThumbnailCache::DiskPath( assetPath );
        if ( ShouldQueue( assetPath, png ) )
        {
            m_Queue.push_back( { Kind::Material, material, Assets::AssetHandle( static_cast<uint64_t>( 0 ) ),
                                 assetPath, png, flatPreview } );
            m_Queued.insert( assetPath );
        }
        return png;
    }

    std::string ThumbnailService::RequestMesh( const Assets::AssetHandle& mesh, const std::string& assetPath,
                                               const Assets::AssetHandle& material )
    {
        const std::string png = ThumbnailCache::DiskPath( assetPath );
        if ( ShouldQueue( assetPath, png ) )
        {
            m_Queue.push_back( { Kind::Mesh, mesh, material, assetPath, png, false } );
            m_Queued.insert( assetPath );
        }
        return png;
    }

    void ThumbnailService::Invalidate( const std::string& assetPath )
    {
        m_Failed.erase( assetPath );
        m_Queued.erase( assetPath );
    }

    void ThumbnailService::Tick()
    {
        // Nothing to preview this session -> never pay for the renderer (it owns a full SceneRenderer).
        if ( m_Queue.empty() && !m_Renderer )
            return;

        if ( !m_Renderer )
            m_Renderer = std::make_unique<AssetThumbnailRenderer>();

        m_Renderer->Tick();

        // Resolve the capture that was in flight.
        if ( !m_InFlight.empty() )
        {
            if ( !m_Renderer->HasPending() )
            {
                std::error_code ec;
                if ( !std::filesystem::exists( m_InFlightPng, ec ) )
                {
                    // The renderer finished but produced nothing — the asset cannot be previewed. Remember
                    // it, or every frame from now on would re-queue the same doomed request.
                    LOG_WARN( "[Thumbnails] no preview produced for '{}' — not retrying", m_InFlight );
                    m_Failed.insert( m_InFlight );
                }
                m_Queued.erase( m_InFlight );
                m_InFlight.clear();
                m_InFlightPng.clear();
                m_InFlightTicks = 0;
            }
            else if ( ++m_InFlightTicks > kInFlightGiveUpTicks )
            {
                LOG_WARN( "[Thumbnails] '{}' never completed — giving up so the queue can drain",
                          m_InFlight );
                m_Failed.insert( m_InFlight );
                m_Queued.erase( m_InFlight );
                m_InFlight.clear();
                m_InFlightPng.clear();
                m_InFlightTicks = 0;
            }
            return; // one capture at a time — the renderer has a single slot
        }

        if ( m_Queue.empty() || m_Renderer->HasPending() )
            return;

        const Request req = m_Queue.front();
        m_Queue.erase( m_Queue.begin() );

        if ( req.Type == Kind::Material )
            m_Renderer->RequestMaterial( req.Handle, req.Png, req.Flat );
        else
            m_Renderer->RequestMesh( req.Handle, req.Png, req.Material );

        m_InFlight      = req.AssetPath;
        m_InFlightPng   = req.Png;
        m_InFlightTicks = 0;
    }
} // namespace Desert::Editor

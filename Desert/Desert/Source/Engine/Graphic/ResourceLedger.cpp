#include <Engine/Graphic/ResourceLedger.hpp>

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace Desert::Graphic
{
    namespace
    {
        struct LedgerRow
        {
            ResourceKind        Kind  = ResourceKind::Image2D;
            ResourceOwner       Owner = ResourceOwner::Unclaimed;
            Common::AssetHandle Asset;
            std::size_t         Bytes      = 0;
            bool                BytesKnown = false;
        };

        /// The rows, and the id that indexes them.
        ///
        /// A MAP AND NOT A VECTOR WITH A FREE LIST, and the reason is the defect this file's neighbour
        /// already has: `ImageService` indexes a vector by a recycled handle and resolves on the INDEX
        /// alone, so a stale handle silently resolves to whoever moved into the slot. A monotonically
        /// increasing id that is never reused cannot do that: a token from a released row finds nothing,
        /// which is the only safe answer. Ids are 64-bit, so exhausting them is not a scenario.
        ///
        /// Function-local statics rather than file-scope ones because GPU objects are constructed during
        /// static initialisation in some translation units (the default textures), and a file-scope map
        /// might not be alive yet. The `Meyers` form gives construction on first use in every order.
        std::mutex& Lock()
        {
            static std::mutex lock;
            return lock;
        }

        std::unordered_map<uint64_t, LedgerRow>& Rows()
        {
            static std::unordered_map<uint64_t, LedgerRow> rows;
            return rows;
        }

        uint64_t& NextRowId()
        {
            static uint64_t next = 1; // 0 is "accounts for nothing" and is never handed out
            return next;
        }

        /// The attribution a new row gets when nobody claims it. Thread-local: two threads building GPU
        /// objects at once must not attribute each other's, and the preloader does run staged work.
        ResourceOwner& AmbientOwner()
        {
            static thread_local ResourceOwner owner = ResourceOwner::Unclaimed;
            return owner;
        }
    } // namespace

    ResourceAttributionScope::ResourceAttributionScope( const ResourceOwner owner ) noexcept
         : m_Previous( AmbientOwner() )
    {
        AmbientOwner() = owner;
    }

    ResourceAttributionScope::~ResourceAttributionScope()
    {
        AmbientOwner() = m_Previous;
    }

    // ────────────────────────────────────────────────────────────────────────────────────────────────
    // ResourceOwnership — the token
    // ────────────────────────────────────────────────────────────────────────────────────────────────

    ResourceOwnership ResourceOwnership::Take( const ResourceKind kind, const std::size_t bytes )
    {
        return ResourceOwnership( ResourceLedger::Open( kind, bytes ) );
    }

    ResourceOwnership::~ResourceOwnership()
    {
        if ( m_Row != 0 )
            ResourceLedger::Close( m_Row );
    }

    ResourceOwnership::ResourceOwnership( ResourceOwnership&& other ) noexcept : m_Row( other.m_Row )
    {
        other.m_Row = 0;
    }

    ResourceOwnership& ResourceOwnership::operator=( ResourceOwnership&& other ) noexcept
    {
        if ( this != &other )
        {
            // The row this token already held is closed FIRST. Overwriting it would leave a row with no
            // token — the exact "reports objects that are gone" drift the header refuses to allow.
            if ( m_Row != 0 )
                ResourceLedger::Close( m_Row );
            m_Row       = other.m_Row;
            other.m_Row = 0;
        }
        return *this;
    }

    void ResourceOwnership::Claim( const ResourceOwner owner, const Common::AssetHandle asset )
    {
        if ( m_Row != 0 )
            ResourceLedger::Attribute( m_Row, owner, asset );
    }

    void ResourceOwnership::RecordBytes( const std::size_t bytes )
    {
        if ( m_Row != 0 )
            ResourceLedger::SetBytes( m_Row, bytes );
    }

    ResourceOwner ResourceOwnership::GetOwner() const
    {
        ResourceOwner       owner = ResourceOwner::Unclaimed;
        Common::AssetHandle asset;
        if ( m_Row != 0 )
            (void)ResourceLedger::Read( m_Row, owner, asset );
        return owner;
    }

    Common::AssetHandle ResourceOwnership::GetAsset() const
    {
        ResourceOwner       owner = ResourceOwner::Unclaimed;
        Common::AssetHandle asset;
        if ( m_Row != 0 )
            (void)ResourceLedger::Read( m_Row, owner, asset );
        return asset;
    }

    // ────────────────────────────────────────────────────────────────────────────────────────────────
    // ResourceLedger — the storage
    // ────────────────────────────────────────────────────────────────────────────────────────────────

    uint64_t ResourceLedger::Open( const ResourceKind kind, const std::size_t bytes )
    {
        std::lock_guard<std::mutex> guard( Lock() );

        const uint64_t id = NextRowId()++;

        LedgerRow row;
        row.Kind = kind;
        // The ambient default, if a scope is open. Nothing else reads it: a later Claim() overwrites the
        // owner outright, because naming the asset behind an object is more specific than naming the
        // subsystem that happened to be building when it appeared.
        row.Owner      = AmbientOwner();
        row.Bytes      = bytes;
        row.BytesKnown = bytes != 0;
        Rows().emplace( id, row );

        return id;
    }

    void ResourceLedger::Close( const uint64_t row )
    {
        std::lock_guard<std::mutex> guard( Lock() );
        Rows().erase( row );
    }

    void ResourceLedger::Attribute( const uint64_t row, const ResourceOwner owner,
                                    const Common::AssetHandle asset )
    {
        std::lock_guard<std::mutex> guard( Lock() );
        if ( const auto it = Rows().find( row ); it != Rows().end() )
        {
            it->second.Owner = owner;
            it->second.Asset = asset;
        }
    }

    void ResourceLedger::SetBytes( const uint64_t row, const std::size_t bytes )
    {
        std::lock_guard<std::mutex> guard( Lock() );
        if ( const auto it = Rows().find( row ); it != Rows().end() )
        {
            it->second.Bytes      = bytes;
            it->second.BytesKnown = bytes != 0;
        }
    }

    bool ResourceLedger::Read( const uint64_t row, ResourceOwner& owner, Common::AssetHandle& asset )
    {
        std::lock_guard<std::mutex> guard( Lock() );
        const auto                  it = Rows().find( row );
        if ( it == Rows().end() )
            return false;
        owner = it->second.Owner;
        asset = it->second.Asset;
        return true;
    }

    ResourceCensus ResourceLedger::Take()
    {
        std::lock_guard<std::mutex> guard( Lock() );

        ResourceCensus census;
        for ( const auto& [id, row] : Rows() )
        {
            ++census.Live;
            ++census.PerKind[static_cast<std::size_t>( row.Kind )];
            ++census.PerOwner[static_cast<std::size_t>( row.Owner )];
            ++census.PerOwnerKind[static_cast<std::size_t>( row.Owner )][static_cast<std::size_t>( row.Kind )];

            if ( row.Owner == ResourceOwner::Unclaimed )
                ++census.Unclaimed;
            if ( static_cast<uint64_t>( row.Asset ) == 0 )
                ++census.WithoutAsset;
            if ( row.BytesKnown )
            {
                census.Bytes += row.Bytes;
                ++census.BytesKnownFor;
            }
        }
        return census;
    }

    std::vector<Common::AssetHandle> ResourceLedger::AssetBackedHandles()
    {
        std::lock_guard<std::mutex> guard( Lock() );

        std::vector<Common::AssetHandle> handles;
        for ( const auto& [id, row] : Rows() )
        {
            if ( row.Owner != ResourceOwner::AssetService )
                continue;
            if ( static_cast<uint64_t>( row.Asset ) == 0 )
                continue;
            handles.push_back( row.Asset );
        }

        std::sort( handles.begin(), handles.end(), []( const Common::AssetHandle& a, const Common::AssetHandle& b )
                   { return static_cast<uint64_t>( a ) < static_cast<uint64_t>( b ); } );
        handles.erase( std::unique( handles.begin(), handles.end() ), handles.end() );
        return handles;
    }

    uint32_t ResourceLedger::RowsFor( const Common::AssetHandle asset )
    {
        std::lock_guard<std::mutex> guard( Lock() );

        uint32_t count = 0;
        for ( const auto& [id, row] : Rows() )
        {
            if ( static_cast<uint64_t>( row.Asset ) == static_cast<uint64_t>( asset ) )
                ++count;
        }
        return count;
    }

    std::string ResourceLedger::Report()
    {
        const ResourceCensus census = Take();

        std::string text;
        text += "live=" + std::to_string( census.Live );
        text += " unclaimed=" + std::to_string( census.Unclaimed );
        text += " without-asset=" + std::to_string( census.WithoutAsset );
        // Both numbers, always. "12 MiB" over 400 rows of which 9 reported a size is a different fact from
        // "12 MiB" over 400 rows that all did, and a reader who is not told cannot separate them.
        text += " bytes=" + std::to_string( census.Bytes ) + " (known for " +
                std::to_string( census.BytesKnownFor ) + " of " + std::to_string( census.Live ) + ")";

        for ( std::size_t owner = 0; owner < static_cast<std::size_t>( ResourceOwner::Count ); ++owner )
        {
            if ( census.PerOwner[owner] == 0 )
                continue;

            text += "\n  ";
            text += ResourceOwnerName( static_cast<ResourceOwner>( owner ) );
            text += " = " + std::to_string( census.PerOwner[owner] ) + ":";

            for ( std::size_t kind = 0; kind < static_cast<std::size_t>( ResourceKind::Count ); ++kind )
            {
                if ( census.PerOwnerKind[owner][kind] == 0 )
                    continue;
                text += ' ';
                text += ResourceKindName( static_cast<ResourceKind>( kind ) );
                text += '=' + std::to_string( census.PerOwnerKind[owner][kind] );
            }
        }

        return text;
    }

} // namespace Desert::Graphic

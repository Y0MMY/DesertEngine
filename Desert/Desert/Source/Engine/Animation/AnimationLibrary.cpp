#include "AnimationLibrary.hpp"

#include <Common/Core/Logger.hpp>

#include <algorithm>

namespace Desert::Animation
{
    AnimationLibrary::AnimationLibrary( Assets::AssetManager* assetManager ) : m_AssetManager( assetManager )
    {
    }

    // RELOAD BEFORE HANDING ONE OUT. Both lookups below resolve a handle the index recorded at Register
    // time, and an evicted clip resolves to a perfectly valid asset holding an empty track list — so
    // without this the caller gets a successful answer that animates nothing. AssetBase::EnsureLoaded is a
    // no-op for a clip that is already resident, which is every clip in the common case.
    //
    // A clip that CANNOT be reloaded is named and skipped rather than returned empty: a procedural clip
    // (SetInMemoryClip) is never evicted, so reaching this branch means the file is gone.
    Assets::Asset<Assets::AnimationAsset> AnimationLibrary::Resolve( const Assets::AssetHandle& handle ) const
    {
        auto asset = m_AssetManager->FindByHandle<Assets::AnimationAsset>( handle );
        if ( !asset )
            return nullptr;

        if ( const auto loaded = asset->EnsureLoaded( *m_AssetManager ); !loaded )
        {
            LOG_ERROR( "[AnimationLibrary] clip '{}' is indexed for a rig but could not be loaded: {}. It is "
                       "not offered; a caller given it would have played an empty clip.",
                       asset->GetMetadata().Filepath.string(), loaded.GetError() );
            return nullptr;
        }

        return asset;
    }

    void AnimationLibrary::Register( const Assets::Asset<Assets::AnimationAsset>& animation )
    {
        if ( !animation )
        {
            return;
        }

        const uint64_t sig    = animation->GetSkeletonSignature();
        const auto     handle = animation->GetMetadata().Handle;

        m_Index[sig].push_back( handle );

        // Record the animated-bone names for tolerant (subset) matching.
        AnimBones entry;
        entry.Handle = handle;
        for ( const auto& track : animation->GetClip().Tracks )
            if ( !track.BoneName.empty() )
                entry.Bones.push_back( track.BoneName );
        m_Anims.push_back( std::move( entry ) );
    }

    void AnimationLibrary::Unregister( const Assets::AssetHandle& handle )
    {
        for ( auto& [sig, list] : m_Index )
        {
            list.erase( std::remove( list.begin(), list.end(), handle ), list.end() );
        }
        m_Anims.erase( std::remove_if( m_Anims.begin(), m_Anims.end(),
                                       [&]( const AnimBones& a ) { return a.Handle == handle; } ),
                       m_Anims.end() );
    }

    std::vector<Assets::Asset<Assets::AnimationAsset>>
    AnimationLibrary::GetForSkeletonBones( const std::unordered_set<std::string>& skeletonBones ) const
    {
        std::vector<Assets::Asset<Assets::AnimationAsset>> result;

        for ( const auto& anim : m_Anims )
        {
            if ( anim.Bones.empty() )
                continue;

            // Fraction of the animation's bones that exist in this skeleton. A real match for the rig is ~1.0
            // (the anim bones are a subset of the character's); require a majority so an odd stray bone or a
            // partial-body clip still matches, while unrelated rigs (different bone names) are rejected.
            size_t present = 0;
            for ( const auto& bone : anim.Bones )
                if ( skeletonBones.count( bone ) )
                    ++present;

            if ( present * 2 >= anim.Bones.size() ) // >= 50%
            {
                if ( auto asset = Resolve( anim.Handle ) )
                    result.push_back( asset );
            }
        }

        return result;
    }

    std::vector<Assets::Asset<Assets::AnimationAsset>>
    AnimationLibrary::GetBySkeleton( uint64_t skeletonSignature ) const
    {
        std::vector<Assets::Asset<Assets::AnimationAsset>> result;

        if ( auto it = m_Index.find( skeletonSignature ); it != m_Index.end() )
        {
            for ( const auto& handle : it->second )
            {
                if ( auto asset = Resolve( handle ) )
                {
                    result.push_back( asset );
                }
            }
        }

        return result;
    }

    void AnimationLibrary::Clear()
    {
        m_Index.clear();
        // m_Anims WAS LEFT BEHIND. Clear() dropped the signature index and kept the bone-name index, so
        // GetForSkeletonBones went on offering every clip of the previous project — resolved through an
        // AssetManager that no longer holds them. Two indexes of one thing, one of which was cleared.
        m_Anims.clear();
    }
} // namespace Desert::Animation
#include "AnimationLibrary.hpp"

namespace Desert::Animation
{
    AnimationLibrary::AnimationLibrary( const Assets::AssetManager* assetManager ) : m_AssetManager( assetManager )
    {
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
                if ( auto asset = m_AssetManager->FindByHandle<Assets::AnimationAsset>( anim.Handle ) )
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
                if ( auto asset = m_AssetManager->FindByHandle<Assets::AnimationAsset>( handle ) )
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
    }
} // namespace Desert::Animation
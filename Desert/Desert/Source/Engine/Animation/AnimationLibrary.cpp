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
    }

    void AnimationLibrary::Unregister( const Assets::AssetHandle& handle )
    {
        for ( auto& [sig, list] : m_Index )
        {
            list.erase( std::remove( list.begin(), list.end(), handle ), list.end() );
        }
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
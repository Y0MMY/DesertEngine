#include "SkyboxCache.hpp"

#include <Engine/Graphic/Materials/MaterialFactory.hpp>

namespace Desert::Runtime
{
    SkyboxCache::SkyboxCache( const std::weak_ptr<Assets::AssetManager>& assetManager )
         : m_AssetManager( assetManager )
    {
    }

    ResourceHandle SkyboxCache::Create( Assets::AssetHandle materialHandle )
    {
        uint32_t index;

        if ( !m_FreeList.empty() )
        {
            index = m_FreeList.front();
            m_FreeList.pop();
        }
        else
        {
            index = static_cast<uint32_t>( m_Entries.size() );
            m_Entries.push_back( {} );
        }

        if ( const auto& assetManager = m_AssetManager.lock() )
        {
            auto materialAsset = assetManager->FindByHandle<Assets::TextureAsset>( materialHandle );

            m_Entries[index].Instance = Graphic::MaterialFactory::CreateSkybox( materialAsset );
            m_Entries[index].IsAlive  = true;

            ResourceHandle handle{ index };
            m_DirtyMaterials.push_back( handle );

            return handle;
        }
        return { ResourceHandle::InvalidIndex };
    }

    ResourceHandle SkyboxCache::Create( const Common::Filepath& path )
    {
        uint32_t index;

        if ( !m_FreeList.empty() )
        {
            index = m_FreeList.front();
            m_FreeList.pop();
        }
        else
        {
            index = static_cast<uint32_t>( m_Entries.size() );
            m_Entries.push_back( {} );
        }

        if ( const auto& assetManager = m_AssetManager.lock() )
        {
            auto materialAsset = assetManager->CreateAsset<Assets::TextureAsset>(
                 Assets::AssetPriority::Low, path, true, Assets::TextureAsset::Type::Skybox );
            if ( !materialAsset )
            {
                return { ResourceHandle::InvalidIndex };
            }

            m_Entries[index].Instance = Graphic::MaterialFactory::CreateSkybox( materialAsset );
            m_Entries[index].IsAlive  = true;

            ResourceHandle handle{ index };
            m_DirtyMaterials.push_back( handle );

            return handle;
        }
        return { ResourceHandle::InvalidIndex };
    }

    void SkyboxCache::Destroy( ResourceHandle handle )
    {
    }

    std::shared_ptr<Graphic::MaterialSkybox> SkyboxCache::Get( ResourceHandle handle )
    {
        if ( !handle.IsValid() || handle.Index >= m_Entries.size() || !m_Entries[handle.Index].IsAlive )
        {
            return nullptr;
        }
        return m_Entries[handle.Index].Instance;
    }

    const std::shared_ptr<Graphic::MaterialSkybox> SkyboxCache::Get( ResourceHandle handle ) const
    {
        if ( !handle.IsValid() || handle.Index >= m_Entries.size() || !m_Entries[handle.Index].IsAlive )
        {
            return nullptr;
        }
        return m_Entries[handle.Index].Instance;
    }

} // namespace Desert::Runtime
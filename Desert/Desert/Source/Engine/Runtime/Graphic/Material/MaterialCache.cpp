#include "MaterialCache.hpp"

#include <Engine/Graphic/Materials/MaterialFactory.hpp>

namespace Desert::Runtime
{
    MaterialCache::MaterialCache( const std::shared_ptr<Assets::AssetManager>& assetManager )
         : m_AssetManager( assetManager )
    {
    }

    ResourceHandle MaterialCache::Create( Assets::AssetHandle materialHandle )
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

        auto materialAsset = m_AssetManager->FindByHandle<Assets::MaterialAsset>( materialHandle );
        if ( !materialAsset )
        {
            return { ResourceHandle::InvalidIndex };
        }

        m_Entries[index].Instance = Graphic::MaterialFactory::Create( materialAsset );
        m_Entries[index].IsAlive  = true;

        ResourceHandle handle{ index };
        m_DirtyMaterials.push_back( handle );

        return handle;
    }

    ResourceHandle MaterialCache::Create( const Common::Filepath& path )
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

        auto materialAsset =
             m_AssetManager->CreateAsset<Assets::MaterialAsset>( Assets::AssetPriority::Low, path );
        if ( !materialAsset )
        {
            return { ResourceHandle::InvalidIndex };
        }

        m_Entries[index].Instance = Graphic::MaterialFactory::Create( materialAsset );
        m_Entries[index].IsAlive  = true;

        ResourceHandle handle{ index };
        m_DirtyMaterials.push_back( handle );

        return handle;
    }

    void MaterialCache::Destroy( ResourceHandle handle )
    {
    }

    Desert::Graphic::MaterialInstance* MaterialCache::Get( ResourceHandle handle )
    {
        if ( !handle.IsValid() || handle.Index >= m_Entries.size() || !m_Entries[handle.Index].IsAlive )
        {
            return nullptr;
        }
        return m_Entries[handle.Index].Instance.get();
    }

    const Desert::Graphic::MaterialInstance* MaterialCache::Get( ResourceHandle handle ) const
    {
        if ( !handle.IsValid() || handle.Index >= m_Entries.size() || !m_Entries[handle.Index].IsAlive )
        {
            return nullptr;
        }
        return m_Entries[handle.Index].Instance.get();
    }

    void MaterialCache::UpdateDirtyMaterials()
    {
    }

} // namespace Desert::Runtime
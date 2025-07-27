#include "MaterialCache.hpp"

#include <Engine/Graphic/Materials/MaterialFactory.hpp>

namespace Desert::Runtime
{
    MaterialCache::MaterialCache( const std::weak_ptr<Assets::AssetManager>& assetManager )
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

        if ( const auto& assetManager = m_AssetManager.lock() )
        {
            auto materialAsset = assetManager->FindByHandle<Assets::MaterialAsset>( materialHandle );

            m_Entries[index].Instance = Graphic::MaterialFactory::CreatePBR( materialAsset );
            m_Entries[index].IsAlive  = true;

            ResourceHandle handle{ index };
            m_DirtyMaterials.push_back( handle );

            return handle;
        }
        return { ResourceHandle::InvalidIndex };
    }

    void MaterialCache::Destroy( ResourceHandle handle )
    {
    }

    std::shared_ptr<Graphic::MaterialPBR> MaterialCache::Get( ResourceHandle handle )
    {
        if ( !handle.IsValid() || handle.Index >= m_Entries.size() || !m_Entries[handle.Index].IsAlive )
        {
            return nullptr;
        }
        return m_Entries[handle.Index].Instance;
    }

    const std::shared_ptr<Graphic::MaterialPBR> MaterialCache::Get( ResourceHandle handle ) const
    {
        if ( !handle.IsValid() || handle.Index >= m_Entries.size() || !m_Entries[handle.Index].IsAlive )
        {
            return nullptr;
        }
        return m_Entries[handle.Index].Instance;
    }

    void MaterialCache::UpdateDirtyMaterials()
    {
    }

} // namespace Desert::Runtime
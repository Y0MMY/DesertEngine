#include "MeshCache.hpp"

#include <Engine/Graphic/Geometry/MeshFactory.hpp>

namespace Desert::Runtime
{
    MeshCache::MeshCache( const std::weak_ptr<Assets::AssetManager>& assetManager )
         : m_AssetManager( assetManager )
    {
    }

    ResourceHandle MeshCache::Create( Assets::AssetHandle meshHandle )
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
            m_Entries.emplace_back();
        }

        if ( const auto& assetManager = m_AssetManager.lock() )
        {
            auto meshAsset = assetManager->FindByHandle<Assets::MeshAsset>( meshHandle );
            if ( !meshAsset )
            {
                return { ResourceHandle::InvalidIndex };
            }

            m_Entries[index].Mesh    = Graphic::MeshFactory::Create( meshAsset );
            m_Entries[index].IsAlive = true;

            return ResourceHandle{ index };
        }
        return { ResourceHandle::InvalidIndex };
    }

    ResourceHandle MeshCache::Create( const Common::Filepath& filepath )
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
            m_Entries.emplace_back();
        }

        if ( const auto& assetManager = m_AssetManager.lock() )
        {
            auto meshAsset = assetManager->CreateAsset<Assets::MeshAsset>( Assets::AssetPriority::Low, filepath );
            if ( !meshAsset )
            {
                return { ResourceHandle::InvalidIndex };
            }

            m_Entries[index].Mesh    = Graphic::MeshFactory::Create( meshAsset );
            m_Entries[index].IsAlive = true;

            return ResourceHandle{ index };
        }
        return { ResourceHandle::InvalidIndex };
    }

    void MeshCache::Destroy( ResourceHandle handle )
    {
    }

    Graphic::MeshInstance* MeshCache::Get( ResourceHandle handle )
    {
        if ( handle.IsValid() && handle.Index < m_Entries.size() && m_Entries[handle.Index].IsAlive )
        {
            return m_Entries[handle.Index].Mesh.get();
        }
        return nullptr;
    }

    const Graphic::MeshInstance* MeshCache::Get( ResourceHandle handle ) const
    {
        if ( handle.IsValid() && handle.Index < m_Entries.size() && m_Entries[handle.Index].IsAlive )
        {
            return m_Entries[handle.Index].Mesh.get();
        }
        return nullptr;
    }

    void MeshCache::Release()
    {
    }

} // namespace Desert::Runtime
#include "ResourceResolver.hpp"

namespace Desert::Runtime
{
    template <typename T, typename CacheGetter, typename ResourceGetter>
    std::shared_ptr<T> ResolveResource( Assets::AssetHandle                                               handle,
                                        std::unordered_map<Assets::AssetHandle, Runtime::ResourceHandle>& cache,
                                        CacheGetter&& cacheGetter, ResourceGetter&& resourceGetter )
    {
        if ( const auto cachedIt = cache.find( handle ); cachedIt != cache.end() )
        {
            return resourceGetter( cachedIt->second );
        }

        const auto runtimeHandle = cacheGetter().Create( handle );
        if ( !runtimeHandle.IsValid() )
        {
            return nullptr;
        }

        cache.emplace( handle, runtimeHandle );
        return resourceGetter( runtimeHandle );
    }

    std::shared_ptr<Desert::Mesh> ResourceResolver::ResolveMesh( Assets::AssetHandle handle )
    {
        return ResolveResource<Desert::Mesh>(
             handle, m_HandleCache,
             [this]() -> auto& { return m_ResourceManager->GetGeometryResources()->GetMeshCache(); },
             [this]( Runtime::ResourceHandle h ) { return TryGetMesh( h ); } );
    }

    std::shared_ptr<Desert::Graphic::MaterialSkybox> ResourceResolver::ResolveSkybox( Assets::AssetHandle handle )
    {
        return ResolveResource<Desert::Graphic::MaterialSkybox>(
             handle, m_HandleCache, [this]() -> auto& { return m_ResourceManager->GetSkyboxCache(); },
             [this]( Runtime::ResourceHandle h ) { return TryGetSkybox( h ); } );
    }

    std::shared_ptr<Desert::Mesh> ResourceResolver::TryGetMesh( Runtime::ResourceHandle handle ) const
    {
        if ( const auto& mesh = m_ResourceManager->GetGeometryResources()->GetMeshCache().Get( handle ) )
        {
            return mesh->GetMesh();
        }
        return nullptr;
    }

    std::shared_ptr<Desert::Graphic::MaterialSkybox>
    ResourceResolver::TryGetSkybox( Runtime::ResourceHandle handle ) const
    {
        if ( const auto& skybox = m_ResourceManager->GetSkyboxCache().Get( handle ) )
        {
            return skybox;
        }
        return nullptr;
    }
} // namespace Desert::Runtime
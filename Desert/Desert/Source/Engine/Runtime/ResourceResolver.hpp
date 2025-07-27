#pragma once

#include "RuntimeResourceManager.hpp"

namespace Desert::Runtime
{
    class ResourceResolver
    {
    public:
        ResourceResolver( const std::shared_ptr<Assets::AssetManager>& resourceManager )
             : m_ResourceManager( std::make_shared<Runtime::RuntimeResourceManager>( resourceManager ) )
        {
        }

        // TODO: Result<>(maybe)

        std::shared_ptr<Mesh>                    ResolveMesh( Assets::AssetHandle handle );
        std::shared_ptr<Graphic::MaterialPBR>    ResolveMaterial( Assets::AssetHandle handle );
        std::shared_ptr<Graphic::MaterialSkybox> ResolveSkybox( Assets::AssetHandle handle );

    private:
        std::shared_ptr<Desert::Mesh>            TryGetMesh( Runtime::ResourceHandle handle ) const;
        std::shared_ptr<Graphic::MaterialPBR>    TryGetMaterial( Runtime::ResourceHandle handle ) const;
        std::shared_ptr<Graphic::MaterialSkybox> TryGetSkybox( Runtime::ResourceHandle handle ) const;

    private:
        std::shared_ptr<Runtime::RuntimeResourceManager> m_ResourceManager;

        std::unordered_map<Assets::AssetHandle, Runtime::ResourceHandle> m_HandleCache;
    };
} // namespace Desert::Runtime
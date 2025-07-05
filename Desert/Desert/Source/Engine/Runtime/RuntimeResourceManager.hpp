#pragma once

#include "Graphic/Material/MaterialCache.hpp"
#include "Graphic/Material/SkyboxCache.hpp"
#include "Graphic/Geometry/MeshCache.hpp"
#include <Engine/Assets/AssetManager.hpp>

namespace Desert::Runtime
{
    class RuntimeResourceManager final
    {
    public:
        explicit RuntimeResourceManager( const std::shared_ptr<Assets::AssetManager>& assetManager )
             : m_AssetManager( assetManager ),
               m_MaterialCache( std::make_unique<MaterialCache>( m_AssetManager ) ),
               m_MeshCache( std::make_unique<MeshCache>( m_AssetManager ) ),
               m_SkyboxCache( std::make_unique<SkyboxCache>( m_AssetManager ) )
        {
        }

        MaterialCache& GetMaterialCache()
        {
            return *m_MaterialCache;
        }

        const MaterialCache& GetMaterialCache() const
        {
            return *m_MaterialCache;
        }

        MeshCache& GetMeshCache()
        {
            return *m_MeshCache;
        }

        const MeshCache& GetMeshCache() const
        {
            return *m_MeshCache;
        }

        SkyboxCache& GetSkyboxCache()
        {
            return *m_SkyboxCache;
        }

        const SkyboxCache& GetSkyboxCache() const
        {
            return *m_SkyboxCache;
        }

        void Shutdown()
        {
        }

    private:
        const std::weak_ptr<Assets::AssetManager> m_AssetManager;
        const std::unique_ptr<MaterialCache>      m_MaterialCache;
        const std::unique_ptr<MeshCache>          m_MeshCache;
        const std::unique_ptr<SkyboxCache>        m_SkyboxCache;
    };
} // namespace Desert::Runtime
#pragma once

#include "Graphic/Material/MaterialCache.hpp"
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
               m_MeshCache( std::make_unique<MeshCache>( m_AssetManager ) )
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

    private:
        const std::shared_ptr<Assets::AssetManager> m_AssetManager;
        const std::unique_ptr<MaterialCache>        m_MaterialCache;
        const std::unique_ptr<MeshCache>            m_MeshCache;
    };
} // namespace Desert::Runtime
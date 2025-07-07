#pragma once

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Graphic/Materials/Skybox/MaterialSkybox.hpp>
#include <Engine/Runtime/Handle.hpp>

#include <queue>

namespace Desert::Runtime
{
    class SkyboxCache
    {
    public:
        explicit SkyboxCache( const std::weak_ptr<Assets::AssetManager>& assetManager );

        ResourceHandle Create( Assets::AssetHandle materialHandle );
        ResourceHandle Create( const Common::Filepath& path );
        void           Destroy( ResourceHandle handle );

        std::shared_ptr<Graphic::MaterialSkybox>       Get( ResourceHandle handle );
        const std::shared_ptr<Graphic::MaterialSkybox> Get( ResourceHandle handle ) const;

    private:
        struct MaterialEntry
        {
            std::shared_ptr<Graphic::MaterialSkybox> Instance;
            bool                                     IsAlive = false;
        };

        std::weak_ptr<Assets::AssetManager> m_AssetManager;
        std::vector<MaterialEntry>          m_Entries;
        std::queue<uint32_t>                m_FreeList;
        std::vector<ResourceHandle>         m_DirtyMaterials;
    };
} // namespace Desert::Runtime
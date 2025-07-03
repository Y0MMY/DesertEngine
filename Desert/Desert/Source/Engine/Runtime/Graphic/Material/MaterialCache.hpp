#pragma once

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Graphic/Materials/MaterialInstance.hpp>
#include <Engine/Runtime/Handle.hpp>

#include <queue>

namespace Desert::Runtime
{
    class MaterialCache
    {
    public:
        explicit MaterialCache( const std::weak_ptr<Assets::AssetManager>& assetManager );

        ResourceHandle Create( Assets::AssetHandle materialHandle );
        ResourceHandle Create( const Common::Filepath& path );
        void           Destroy( ResourceHandle handle );

        Graphic::MaterialInstance*       Get( ResourceHandle handle );
        const Graphic::MaterialInstance* Get( ResourceHandle handle ) const;

        void UpdateDirtyMaterials();

    private:
        struct MaterialEntry
        {
            std::unique_ptr<Graphic::MaterialInstance> Instance;
            bool                                       IsAlive = false;
        };

        std::weak_ptr<Assets::AssetManager> m_AssetManager;
        std::vector<MaterialEntry>          m_Entries;
        std::queue<uint32_t>                m_FreeList;
        std::vector<ResourceHandle>         m_DirtyMaterials;
    };
} // namespace Desert::Runtime
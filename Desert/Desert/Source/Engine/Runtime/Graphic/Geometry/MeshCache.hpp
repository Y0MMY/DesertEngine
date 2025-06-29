#pragma once

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Graphic/Geometry/MeshInstance.hpp>
#include <Engine/Runtime/Handle.hpp>

#include <queue>

namespace Desert::Runtime
{
    class MeshCache
    {
    public:
        explicit MeshCache( const std::shared_ptr<Assets::AssetManager>& assetManager );

        ResourceHandle Create( Assets::AssetHandle meshHandle );
        void           Destroy( ResourceHandle handle );

        Graphic::MeshInstance*       Get( ResourceHandle handle );
        const Graphic::MeshInstance* Get( ResourceHandle handle ) const;

    private:
        struct MeshEntry
        {
            std::unique_ptr<Graphic::MeshInstance> Mesh;
            bool                                   IsAlive = false;
        };

        std::shared_ptr<Assets::AssetManager> m_AssetManager;
        std::vector<MeshEntry>                m_Entries;
        std::queue<uint32_t>                  m_FreeList;
    };
} // namespace Desert::Runtime
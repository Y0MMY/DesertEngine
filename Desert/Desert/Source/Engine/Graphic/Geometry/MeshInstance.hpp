#pragma once

#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Graphic/Geometry/Mesh.hpp>

namespace Desert::Graphic
{
    class MeshInstance
    {
    public:
        explicit MeshInstance( const Assets::Asset<Assets::MeshAsset>& baseAsset ) : m_BaseMeshAsset( baseAsset )
        {
        }

        Assets::Asset<Assets::MeshAsset> GetMeshAsset() const
        {
            return m_BaseMeshAsset.lock();
        }

        bool IsReady() const
        {
            if ( const auto& baseMeshAsset = m_BaseMeshAsset.lock() )
            {
                return baseMeshAsset->IsReadyForUse();
            }
            return false;
        }

        // Geometry access
        const std::shared_ptr<Mesh> GetMesh() const
        {
            if ( const auto& baseMeshAsset = m_BaseMeshAsset.lock() )
            {
                return baseMeshAsset->GetMesh();
            }
            return nullptr;
        }

    private:
        std::weak_ptr<Assets::MeshAsset> m_BaseMeshAsset;
    };
} // namespace Desert::Graphic
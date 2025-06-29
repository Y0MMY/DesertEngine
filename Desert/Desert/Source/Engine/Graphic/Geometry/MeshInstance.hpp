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
            return m_BaseMeshAsset;
        }

        // Geometry access
        const Mesh* GetMesh() const
        {
            return ( m_BaseMeshAsset->GetMesh().get() );
        }

    private:
        Assets::Asset<Assets::MeshAsset> m_BaseMeshAsset;
    };
} // namespace Desert::Graphic
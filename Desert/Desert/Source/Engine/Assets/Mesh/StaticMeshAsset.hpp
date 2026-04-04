#pragma once

#include "MeshAsset.hpp"

#include <Engine/Geometry/MeshTypes.hpp>

namespace Desert::Assets
{
    class StaticMeshAsset final : public MeshAsset
    {
    public:
        StaticMeshAsset( const AssetPriority priority, const Common::Filepath& filepath );

        Common::BoolResultStr Load() override;
        Common::BoolResultStr Unload() override;

        bool IsSkinned() const override
        {
            return false;
        }

        const std::vector<Vertex>& GetVertices() const
        {
            return m_Vertices;
        }
        const std::vector<Index>& GetIndices() const
        {
            return m_Indices;
        }
        const std::vector<Submesh>& GetSubmeshes() const
        {
            return m_Submeshes;
        }

        bool IsReadyForUse() const override
        {
            return m_IsReadyForUse;
        }

    private:
        std::vector<Vertex>  m_Vertices;
        std::vector<Index>   m_Indices;
        std::vector<Submesh> m_Submeshes;

        bool m_IsReadyForUse = false;
    };
} // namespace Desert::Assets
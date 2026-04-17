#pragma once

#include <Common/Core/Math/AABB.hpp>
#include <Engine/Graphic/IndexBuffer.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>
#include "Errors/MeshError.hpp"
#include "MeshTypes.hpp"

#include <Common/Core/ResultWithCodes.hpp>

#include <unordered_map>

namespace Desert
{
    class Mesh
    {
    public:
        virtual ~Mesh() = default;

        // Common interface
        [[nodiscard]] virtual MeshType GetType() const = 0;
        [[nodiscard]] bool             IsSkinned() const
        {
            return GetType() == MeshType::Skinned;
        }

        [[nodiscard]] virtual const std::vector<Submesh>& GetSubmeshes() const
        {
            return m_Submeshes;
        }
        [[nodiscard]] const std::shared_ptr<Graphic::VertexBuffer>& GetVertexBuffer() const
        {
            return m_VertexBuffer;
        }
        [[nodiscard]] const std::shared_ptr<Graphic::IndexBuffer>& GetIndexBuffer() const
        {
            return m_IndexBuffer;
        }

        [[nodiscard]] virtual Common::BoolResultWithCodes<MeshError> Invalidate() = 0;

    protected:
        // Protected data accessible by derived classes
        std::shared_ptr<Graphic::VertexBuffer>  m_VertexBuffer;
        std::shared_ptr<Graphic::IndexBuffer>   m_IndexBuffer;
        std::vector<std::vector<TriangleCache>> m_TriangleCache;
        std::vector<Submesh>                    m_Submeshes;
    };
} // namespace Desert

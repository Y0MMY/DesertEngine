#pragma once

#include "Mesh.hpp"

namespace Desert::Graphic
{
    class MeshFactory
    {
    public:
        static std::shared_ptr<Mesh> Create( const std::shared_ptr<Assets::MeshAsset>& baseAsset )
        {
            const auto& mesh = std::make_shared<Mesh>( baseAsset );
            mesh->Invalidate();

            return mesh;
        }
    };
} // namespace Desert::Graphic
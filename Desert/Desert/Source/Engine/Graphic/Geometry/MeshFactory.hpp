#pragma once

#include "MeshInstance.hpp"

namespace Desert::Graphic
{
    class MeshFactory
    {
    public:
        static std::unique_ptr<MeshInstance> Create( const std::shared_ptr<Assets::MeshAsset>& baseAsset )
        {
            return std::make_unique<MeshInstance>( baseAsset );
        }
    };
} // namespace Desert::Graphic
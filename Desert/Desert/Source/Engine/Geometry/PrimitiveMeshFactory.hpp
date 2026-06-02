#pragma once

#include <memory>
#include <Engine/Geometry/DynamicMesh.hpp>
#include "PrimitiveType.hpp"

namespace Desert::Geometry
{
    class PrimitiveMeshFactory
    {
    public:
        static std::shared_ptr<DynamicMesh> Create( PrimitiveType type );

    private:
        static std::shared_ptr<DynamicMesh> CreateCube();
        static std::shared_ptr<DynamicMesh> CreateSphere();
        static std::shared_ptr<DynamicMesh> CreatePlane();
        static std::shared_ptr<DynamicMesh> CreatePyramid();
    };
} // namespace Desert::Geometry
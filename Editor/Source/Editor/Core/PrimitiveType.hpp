#pragma once

#include <Engine/Geometry/Mesh.hpp>

namespace Desert::Editor
{
    enum PrimitiveType
    {
        Cube = 0,
        Sphere,
        Pyramid,
        Plane,
        Cylinder,
        Capsule,
        Terrain,
        LightCube,

        Count
    };

} // namespace Desert::Editor
#pragma once

namespace Desert::Geometry
{
    enum class PrimitiveType
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
} // namespace Desert::Geometry
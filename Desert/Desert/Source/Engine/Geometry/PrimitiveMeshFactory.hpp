#pragma once

#include <Engine/Geometry/Mesh.hpp>

#include "PrimitiveType.hpp"

namespace Desert
{
    class PrimitiveMeshFactory
    {
    public:
        static void Initialize();
        static void Shutdown();

        static std::shared_ptr<Mesh> GetPrimitive( PrimitiveType type );
        static const std::string&    GetPrimitiveName( PrimitiveType type );

        static bool IsInitialized()
        {
            return s_Initialized;
        }

    private:
        static void CreateCube();
        static void CreateSphere();
        static void CreatePyramid();
        static void CreatePlane();
        static void CreateCylinder();
        static void CreateCapsule();
        static void CreateTerrain();
        static void CreateLightCube();

    private:
        static bool                                                                           s_Initialized;
        static std::array<std::shared_ptr<Mesh>, static_cast<size_t>( PrimitiveType::Count )> s_Primitives;
        static const std::array<std::string, static_cast<size_t>( PrimitiveType::Count )>     s_PrimitiveNames;
    };
} // namespace Desert
#include "PrimitiveMeshFactory.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

#include <Engine/Geometry/MeshFactory.hpp>

namespace Desert::Editor
{
    std::shared_ptr<DynamicMesh> PrimitiveMeshFactory::Create( PrimitiveType type )
    {
        switch ( type )
        {
            case PrimitiveType::Cube:
            {
                static auto cube = CreateCube();
                return cube;
            }
            case PrimitiveType::Sphere:
                return CreateSphere();
            case PrimitiveType::Plane:
                return CreatePlane();
            case PrimitiveType::Pyramid:
                return CreatePyramid();
            default:
                return nullptr;
        }
    }

    std::shared_ptr<DynamicMesh> PrimitiveMeshFactory::CreateCube()
    {
        std::vector<Vertex>  vertices;
        std::vector<Index>   indices;
        std::vector<Submesh> submeshes;

        return std::make_shared<DynamicMesh>( vertices, indices, submeshes );
    }

    std::shared_ptr<Desert::DynamicMesh> PrimitiveMeshFactory::CreateSphere()
    {
        return nullptr;
    }

    std::shared_ptr<Desert::DynamicMesh> PrimitiveMeshFactory::CreatePlane()
    {
        return nullptr;
    }

    std::shared_ptr<Desert::DynamicMesh> PrimitiveMeshFactory::CreatePyramid()
    {
        return nullptr;
    }

} // namespace Desert::Editor
#include "ResourceResolver.hpp"

namespace Desert::Runtime
{

    std::shared_ptr<Desert::Mesh> ResourceResolver::ResolveMesh( Runtime::ResourceHandle handle )
    {
        if ( const auto& manager = m_ResourceManager.lock() )
        {
            if ( const auto& mesh = manager->GetGeometryResources()->GetMeshCache().Get( handle ) )
            {
                return mesh->GetMesh();
            }
        }
        return nullptr;
    }

    std::shared_ptr<Desert::Graphic::MaterialPBR>
    ResourceResolver::ResolveMaterial( Runtime::ResourceHandle handle )
    {
        if ( const auto& manager = m_ResourceManager.lock() )
        {
            if ( const auto& material = manager->GetGeometryResources()->GetMaterialCache().Get( handle ) )
            {
                return material;
            }
        }
        return nullptr;
    }

    std::shared_ptr<Desert::Graphic::MaterialSkybox>
    ResourceResolver::ResolveSkybox( Runtime::ResourceHandle handle )
    {
        if ( const auto& manager = m_ResourceManager.lock() )
        {
            if ( const auto& material = manager->GetSkyboxCache().Get( handle ) )
            {
                return material;
            }
        }
        return nullptr;
    }

} // namespace Desert::Runtime
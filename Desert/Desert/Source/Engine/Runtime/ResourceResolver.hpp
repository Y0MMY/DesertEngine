#pragma once

#include "RuntimeResourceManager.hpp"

namespace Desert::Runtime
{
    class ResourceResolver
    {
    public:
        ResourceResolver( const std::shared_ptr<Runtime::RuntimeResourceManager>& resourceManager )
             : m_ResourceManager( resourceManager )
        {
        }

        // TODO: Result<>(maybe)

        std::shared_ptr<Mesh>                    ResolveMesh( Runtime::ResourceHandle handle );
        std::shared_ptr<Graphic::MaterialPBR>    ResolveMaterial( Runtime::ResourceHandle handle );
        std::shared_ptr<Graphic::MaterialSkybox> ResolveSkybox( Runtime::ResourceHandle handle );

    private:
        std::weak_ptr<Runtime::RuntimeResourceManager> m_ResourceManager;
    };
} // namespace Desert::Runtime
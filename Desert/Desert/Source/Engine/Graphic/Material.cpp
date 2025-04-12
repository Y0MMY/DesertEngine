#include <Engine/Graphic/Material.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanMaterial.hpp>

namespace Desert::Graphic
{

    std::shared_ptr<Material> Material::Create( const std::string&             debugName,
                                                const std::shared_ptr<Shader>& shader )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
            {
                return std::make_shared<API::Vulkan::VulkanMaterial>( debugName, shader );
            }
        }
        return nullptr;
    }

} // namespace Desert::Graphic
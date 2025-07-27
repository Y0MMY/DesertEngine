#include <Engine/imgui/UICacheTextureImGui.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

#include <vulkan/vulkan.hpp>

#include <ImGui/backends/imgui_impl_vulkan.h>

namespace Desert::ImGui
{
    const void* UICacheTextureImGui::AddTextureCache( const std::shared_ptr<Graphic::Image2D>& image )
    {
        if ( !image )
            return nullptr;

        if ( Graphic::RendererAPI::GetAPIType() == Graphic::RendererAPIType::Vulkan )
        {
            static std::unordered_map<VkImageView, ImTextureID> g_TextureCache;

            const auto vulkanImageInfo =
                 sp_cast<Graphic::API::Vulkan::VulkanImage2D>( image )->GetVulkanImageInfo();
            if ( !vulkanImageInfo.ImageInfo.imageView )
                return nullptr;

            auto it = g_TextureCache.find( vulkanImageInfo.ImageInfo.imageView );
            if ( it != g_TextureCache.end() )
            {
                return it->second;
            }

            ImTextureID textureID = ImGui_ImplVulkan_AddTexture(
                 vulkanImageInfo.ImageInfo.sampler, vulkanImageInfo.ImageInfo.imageView, vulkanImageInfo.ImageInfo.imageLayout);

            g_TextureCache[vulkanImageInfo.ImageInfo.imageView] = textureID;
            return textureID;
        }
    }

} // namespace Desert::ImGui
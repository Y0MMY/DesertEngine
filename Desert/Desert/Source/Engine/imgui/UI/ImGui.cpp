#include <Engine/imgui/UI/ImGui.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

#include <ImGui/backends/imgui_impl_vulkan.h>

namespace Desert::ImGui::UI
{

    static std::unordered_map<std::shared_ptr<Graphic::Image2D>, ImTextureID> g_TextureCache;

    void Image( const std::shared_ptr<Graphic::Image2D>& image, const ImVec2& size, const ImVec2& uv0,
                const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
    {
        if ( !image )
            return;

        auto it = g_TextureCache.find( image );
        if ( it != g_TextureCache.end() )
        {
            ::ImGui::Image( it->second, size, uv0, uv1, tint_col, border_col );
            return;
        }

        if ( Graphic::RendererAPI::GetAPIType() == Graphic::RendererAPIType::Vulkan )
        {
            const auto vulkanImageInfo =
                 sp_cast<Graphic::API::Vulkan::VulkanImage2D>( image )->GetVulkanImageInfo();
            if ( !vulkanImageInfo.ImageView )
                return;

            ImTextureID textureID = ImGui_ImplVulkan_AddTexture(
                 vulkanImageInfo.Sampler, vulkanImageInfo.ImageView, vulkanImageInfo.Layout );

            g_TextureCache[image] = textureID;
            ::ImGui::Image( textureID, size, uv0, uv1, tint_col, border_col );
        }
    }

} // namespace Desert::ImGui::UI
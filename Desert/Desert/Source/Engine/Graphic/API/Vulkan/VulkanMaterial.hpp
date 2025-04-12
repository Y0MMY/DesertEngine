#pragma once

#include <Engine/Graphic/Material.hpp>
#include <Engine/Core/Models/Shader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUniformBuffer.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanMaterial final : public Material
    {
    public:
        VulkanMaterial( const std::string& debugName, const std::shared_ptr<Shader>& shader );

        virtual Common::BoolResult SetVec3( const std::string& name, const glm::vec3& data ) override;
        virtual Common::BoolResult SetMat4( const std::string& name, const glm::mat4& data ) override;
        virtual Common::BoolResult SetData( const std::string& name, const void* data,
                                            const uint32_t size ) override;
        virtual Common::BoolResult SetImage2D( const std::string&              name,
                                               const std::shared_ptr<Image2D>& image2D ) override;
        virtual Common::BoolResult ApplyMaterial() override;
        virtual Common::BoolResult Invalidate() override;

    private:
    private:
        const std::shared_ptr<Shader> m_Shader;
        const std::string             m_DebugName;

        struct UniformData
        {
            std::shared_ptr<VulkanUniformBuffer> Buffer;
            uint32_t                             Offset;
            uint32_t                             Size;
            uint32_t                             Binding;
        };

        struct Sampler2DData
        {
            std::shared_ptr<Image2D> Image2D;
            uint32_t                 Binding;
        };

        std::unordered_map<std::string, UniformData>   m_Uniforms;
        std::unordered_map<std::string, Sampler2DData> m_Images2D;
    };
} // namespace Desert::Graphic::API::Vulkan
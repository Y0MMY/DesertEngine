#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Core/Models/Shader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUniformBuffer.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanMaterial final : public Material
    {
    public:
        VulkanMaterial( const std::string& debugName, const std::shared_ptr<Shader>& shader );

        virtual Common::BoolResult
        AddUniformToOverride( const std::shared_ptr<UniformBuffer>& uniformBuffer ) override;
        virtual Common::BoolResult SetImage2D( const std::string&              name,
                                               const std::shared_ptr<Image2D>& image2D ) override;
        virtual Common::BoolResult SetImageCube( const std::string&                name,
                                                 const std::shared_ptr<ImageCube>& imageCube ) override;
        virtual Common::BoolResult ApplyMaterial() override;
        virtual Common::BoolResult Invalidate() override;
        virtual void               Clear() override; // TODO: better func name

        virtual Common::BoolResult PushConstant( const void* buffer, const uint32_t bufferSize ) override;

        const auto& GetPushConstantBuffer() const
        {
            return m_PushConstantBuffer;
        }

    private:
    private:
        const std::shared_ptr<Shader> m_Shader;
        const std::string             m_DebugName;

        struct Sampler2DData
        {
            std::shared_ptr<Image2D> Image2D;
            uint32_t                 Binding;
        };

        struct SamplerCubeData
        {
            std::shared_ptr<ImageCube> ImageCube;
            uint32_t                   Binding;
        };

        std::vector<std::shared_ptr<VulkanUniformBuffer>> m_OverriddenUniforms;

        std::vector<std::pair<std::string, uint32_t>> m_AvalivaleImages2D;
        std::vector<std::pair<std::string, uint32_t>> m_AvalivaleImagesCube;

        std::vector<Sampler2DData>   m_OverriddenImages2D;
        std::vector<SamplerCubeData> m_OverriddenImagesCube;

        Common::Memory::Buffer m_PushConstantBuffer;
    };
} // namespace Desert::Graphic::API::Vulkan
#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Core/Models/Shader.hpp>
#include <Engine/Uniforms/API/Vulkan/VulkanUniformBuffer.hpp>
#include <Engine/Uniforms/API/Vulkan/VulkanUniformImageCube.hpp>
#include <Engine/Uniforms/API/Vulkan/VulkanUniformImage2D.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanMaterial final : public Material
    {
    public:
        VulkanMaterial( const std::string& debugName, const std::shared_ptr<Shader>& shader );

        virtual Common::BoolResult
        AddUniformBufferToOverride( const std::shared_ptr<Uniforms::UniformBuffer>& uniformBuffer ) override;
        virtual Common::BoolResult
        AddUniformCubeToOverride( const std::shared_ptr<Uniforms::UniformImageCube>& uniformCube ) override;
        virtual Common::BoolResult
        AddUniform2DToOverride( const std::shared_ptr<Uniforms::UniformImage2D>& uniform2D ) override;
        virtual Common::BoolResult ApplyMaterial() override;
        virtual Common::BoolResult Invalidate() override;
        virtual void               Clear() override; // TODO: better func name

        virtual Common::BoolResult PushConstant( const void* buffer, const uint32_t bufferSize ) override;

        const auto& GetPushConstantBuffer() const
        {
            return m_PushConstantBuffer;
        }

        virtual std::shared_ptr<Shader> GetShader() const
        {
            return m_Shader;
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

        struct
        {
            std::vector<std::shared_ptr<Uniforms::API::Vulkan::VulkanUniformBuffer>>    Buffers;
            std::vector<std::shared_ptr<Uniforms::API::Vulkan::VulkanUniformImageCube>> ImageCubes;
            std::vector<std::shared_ptr<Uniforms::API::Vulkan::VulkanUniformImage2D>>   Images2D;
        } m_OverriddenUniforms;

        Common::Memory::Buffer m_PushConstantBuffer;
    };
} // namespace Desert::Graphic::API::Vulkan
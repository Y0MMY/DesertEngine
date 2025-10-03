#pragma once

#include <Engine/Core/Formats/Shader.hpp>

//TODO: move to Uniforms global dir
namespace Desert::Core::Models
{
    namespace Common
    {
        struct Field
        {
            Core::Formats::ShaderDataType FieldDataType;
            std::string                   Name;
            uint32_t                      Size;
            uint32_t                      Offset;
            uint32_t                      ArraySize;
        };

    } // namespace Common

    struct UniformBuffer
    {
        uint32_t                   Size         = 0;
        uint32_t                   BindingPoint = 0;
        std::string                Name;
        std::vector<Common::Field> Fields;
        Core::Formats::ShaderStage ShaderStage = Core::Formats::ShaderStage::None;
    };

    struct Image2DSampler
    {
        uint32_t                   BindingPoint  = 0;
        uint32_t                   DescriptorSet = 0;
        uint32_t                   ArraySize     = 0;
        std::string                Name;
        Core::Formats::ShaderStage ShaderStage = Core::Formats::ShaderStage::Fragment;
    };

    struct ImageCubeSampler
    {
        uint32_t                   BindingPoint  = 0;
        uint32_t                   DescriptorSet = 0;
        uint32_t                   ArraySize     = 0;
        std::string                Name;
        Core::Formats::ShaderStage ShaderStage = Core::Formats::ShaderStage::Fragment;
    };

    struct StorageBuffer
    {
        uint32_t                   Size         = 0;
        uint32_t                   BindingPoint = 0;
        std::string                Name;
        Core::Formats::ShaderStage ShaderStage = Core::Formats::ShaderStage::None;
    };

    struct PushConstant
    {
        uint32_t                   Offset = 0;
        uint32_t                   Size   = 0;
        std::string                Name;
        Core::Formats::ShaderStage ShaderStage = Core::Formats::ShaderStage::None;
    };

} // namespace Desert::Core::Models
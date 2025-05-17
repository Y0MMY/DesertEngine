#pragma once

#include <Engine/Core/Formats/Shader.hpp>

namespace Desert::Core::Models
{
    struct UniformBuffer
    {
        uint32_t                      Size         = 0;
        uint32_t                      BindingPoint = 0;
        std::string                   Name;
       // Core::Formats::ShaderDataType Type;

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
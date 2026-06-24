#pragma once

#include <Engine/Core/Formats/Shader.hpp>

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

// TODO: move to Uniforms global dir
namespace Desert::ShaderResources::ShaderLayout
{
    struct ShaderFieldLayout
    {
        Core::Formats::ShaderValueType FieldDataType;
        std::string                    Name;
        uint32_t                       Size;
        uint32_t                       Offset;
        uint32_t                       ArraySize = 1;
    };

    struct UniformBuffer
    {
        uint32_t                        Size         = 0;
        uint32_t                        BindingPoint = 0;
        std::string                     Name;
        std::vector<ShaderFieldLayout>  Fields;
        const Core::Formats::BufferKind BufferKind  = Core::Formats::BufferKind::Uniform;
        Core::Formats::ShaderStage      ShaderStage = Core::Formats::ShaderStage::None;
    };

    struct StorageBuffer
    {
        uint32_t                        Size         = 0;
        uint32_t                        BindingPoint = 0;
        std::string                     Name;
        std::vector<ShaderFieldLayout>  Fields;
        const Core::Formats::BufferKind BufferKind  = Core::Formats::BufferKind::Storage;
        Core::Formats::ShaderStage      ShaderStage = Core::Formats::ShaderStage::None;
    };

    struct Image2DSampler
    {
        uint32_t                   BindingPoint  = 0;
        uint32_t                   DescriptorSet = 0;
        uint32_t                   ArraySize     = 1;
        std::string                Name;
        Core::Formats::ShaderStage ShaderStage = Core::Formats::ShaderStage::Fragment;
    };

    struct ImageCubeSampler
    {
        uint32_t                   BindingPoint  = 0;
        uint32_t                   DescriptorSet = 0;
        uint32_t                   ArraySize     = 1;
        std::string                Name;
        Core::Formats::ShaderStage ShaderStage = Core::Formats::ShaderStage::Fragment;
    };

    struct PushConstantRange
    {
        uint32_t                   Offset = 0;
        uint32_t                   Size   = 0;
        std::string                Name;
        Core::Formats::BufferKind  BufferKind  = Core::Formats::BufferKind::PushConstant;
        Core::Formats::ShaderStage ShaderStage = Core::Formats::ShaderStage::None;
    };

} // namespace Desert::ShaderResources::ShaderLayout
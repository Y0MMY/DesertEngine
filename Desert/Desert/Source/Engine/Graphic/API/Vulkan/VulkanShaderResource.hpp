#pragma once

#include <vulkan/vulkan.h>

#include <Engine/Graphic/RendererTypes.hpp>
#include <Engine/ShaderResources/ShaderReflectionTypes.hpp>

#include <optional>
#include <unordered_map>

namespace Desert::Graphic::API::Vulkan
{
    namespace ShaderResource
    {

        inline constexpr uint32_t MAX_SETS = 1;

        // One reflected descriptor set. A resource lands in exactly ONE bucket, chosen from its
        // declared SPIR-V type (see VulkanShaderReflection.hpp) — never from its name.
        //
        // Why sampled images are split three ways but storage images only two: a sampled binding is
        // seeded with a FALLBACK image before anything writes it (VulkanMaterialBackend::
        // InitializeWithFallbacks), and that fallback must have the matching view type, so 2D, 3D and
        // cube each need their own bucket. A storage binding is written from a mip VIEW the caller
        // supplies (VulkanPipelineCompute::RecordDescriptorsAndDispatch), which already carries its
        // own view type — so nothing downstream needs a storage 2D told apart from a storage cube,
        // and keeping them together leaves the existing IBL compute chain exactly as it was. A volume
        // is split off because the storage path binds 2D and cube images only; a 3D binding has to be
        // recognisable before it can be given a path of its own.
        struct ShaderDescriptorSet
        {
            using UniformBufferMap =
                 std::unordered_map<BindingPoint, ShaderResources::ShaderLayout::UniformBuffer>;
            using ImageSampler2DMap =
                 std::unordered_map<BindingPoint, ShaderResources::ShaderLayout::Image2DSampler>;
            using ImageSampler3DMap =
                 std::unordered_map<BindingPoint, ShaderResources::ShaderLayout::Image3DSampler>;
            using ImageSamplerCubeMap =
                 std::unordered_map<BindingPoint, ShaderResources::ShaderLayout::ImageCubeSampler>;
            using StorageBufferMap =
                 std::unordered_map<BindingPoint, ShaderResources::ShaderLayout::StorageBuffer>;

            UniformBufferMap    UniformBuffers;
            ImageSampler2DMap   Image2DSamplers;
            ImageSampler3DMap   Image3DSamplers;
            ImageSamplerCubeMap ImageCubeSamplers;
            StorageBufferMap    StorageBuffers;
            ImageSampler2DMap   StorageImage2DSamplers; // storage image2D AND imageCube
            ImageSampler3DMap   StorageImage3DSamplers;

            operator bool()
            {
                return !UniformBuffers.empty();
            }
        };

        // Everything reflection extracts from a shader program, merged across its stages. Holds no
        // Vulkan handle, so it can be produced with no device — which is what makes the classification
        // testable off-GPU (Desert/Tests/Engine/ShaderReflection).
        struct ReflectionData
        {
            std::unordered_map<SetPoint, ShaderDescriptorSet> ShaderDescriptorSets; // SetPoint = set

            std::optional<ShaderResources::ShaderLayout::PushConstantRange> PushConstantRanges;
        };

    } // namespace ShaderResource
} // namespace Desert::Graphic::API::Vulkan

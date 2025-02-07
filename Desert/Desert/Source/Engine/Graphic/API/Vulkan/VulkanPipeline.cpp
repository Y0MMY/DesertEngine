#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Core/EngineContext.h>

#include <Engine/Graphic/VertexBuffer.hpp>

namespace Desert::Graphic::API::Vulkan
{

    namespace
    {
        static VkFormat ShaderDataTypeToVulkanFormat( ShaderDataType type )
        {
            switch ( type )
            {
                case ShaderDataType::Float:
                    return VK_FORMAT_R32_SFLOAT;
                case ShaderDataType::Float2:
                    return VK_FORMAT_R32G32_SFLOAT;
                case ShaderDataType::Float3:
                    return VK_FORMAT_R32G32B32_SFLOAT;
                case ShaderDataType::Float4:
                    return VK_FORMAT_R32G32B32A32_SFLOAT;
            }
            return VK_FORMAT_UNDEFINED;
        }

    } // namespace

    VulkanPipeline::VulkanPipeline( const PipelineSpecification& specification ) : m_Specification( specification )
    {
    }

    void VulkanPipeline::Invalidate()
    {
        // Vertex input state used for pipeline creation
        VkVertexInputBindingDescription vertexInputBinding = {};
        VertexBufferLayout&             vertexLayout       = m_Specification.Layout;

        vertexInputBinding.binding   = 0;
        vertexInputBinding.stride    = vertexLayout.GetStride();
        vertexInputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::vector<VkVertexInputAttributeDescription> vertexInputAttributes( vertexLayout.GetElementCount() );

        uint32_t location = 0;
        for ( auto element : vertexLayout )
        {
            vertexInputAttributes[location].binding  = 0;
            vertexInputAttributes[location].location = location;
            vertexInputAttributes[location].format   = ShaderDataTypeToVulkanFormat( element.Type );
            vertexInputAttributes[location].offset   = element.Offset;

            location++;
        }

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
             .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
             .pNext                           = nullptr,
             .vertexBindingDescriptionCount   = 1,
             .pVertexBindingDescriptions      = &vertexInputBinding,
             .vertexAttributeDescriptionCount = (uint32_t)vertexInputAttributes.size(),
             .pVertexAttributeDescriptions    = vertexInputAttributes.data() };

        VkPipelineInputAssemblyStateCreateInfo pipelineIACreateInfo = {
             .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
             .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
             .primitiveRestartEnable = VK_FALSE };

        uint32_t windowWidth  = EngineContext::GetInstance().GetCurrentWindowWidth();
        uint32_t windowHeight = EngineContext::GetInstance().GetCurrentWindowHeight();

        VkViewport VP = { .x        = 0.0f,
                          .y        = 0.0f,
                          .width    = (float)windowWidth,
                          .height   = (float)windowHeight,
                          .minDepth = 0.0f,
                          .maxDepth = 1.0f };

        VkRect2D scissor{ .offset =
                               {
                                    .x = 0,
                                    .y = 0,
                               },

                          .extent = { .width = windowWidth, .height = windowHeight } };

        VkPipelineViewportStateCreateInfo VPCreateInfo = {
             .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
             .viewportCount = 1,
             .pViewports    = &VP,
             .scissorCount  = 1,
             .pScissors     = &scissor };

        VkPipelineRasterizationStateCreateInfo rastCreateInfo = {
             .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
             .polygonMode = VK_POLYGON_MODE_FILL,
             .cullMode    = VK_CULL_MODE_NONE,
             .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
             .lineWidth   = 1.0f };

        VkPipelineMultisampleStateCreateInfo pipelineMSCreateInfo = {
             .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
             .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
             .sampleShadingEnable  = VK_FALSE,
             .minSampleShading     = 1.0f };

        VkPipelineColorBlendAttachmentState BlendAttachState = {
             .blendEnable    = VK_FALSE,
             .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                               VK_COLOR_COMPONENT_A_BIT };
        VkPipelineColorBlendStateCreateInfo BlendCreateInfo = {
             .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
             .logicOpEnable   = VK_FALSE,
             .logicOp         = VK_LOGIC_OP_COPY,
             .attachmentCount = 1,
             .pAttachments    = &BlendAttachState };

        std::shared_ptr<VulkanShader> vulkanShader =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( m_Specification.Shader );

        const auto descriptorSets = vulkanShader->GetShaderDescriptorSets();
        if ( !descriptorSets.empty() )
        {
            vulkanShader->CreateDescriptorSets( 3 );
        }

        const auto descriptorSetLayouts = vulkanShader->GetAllDescriptorSetLayouts();

        VkPipelineLayoutCreateInfo LayoutInfo = { .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                  .setLayoutCount = (uint32_t)descriptorSetLayouts.size(),
                                                  .pSetLayouts    = descriptorSetLayouts.data() };

        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        VkResult res = vkCreatePipelineLayout( device, &LayoutInfo, VK_NULL_HANDLE, &m_PipelineLayout );

        std::shared_ptr<VulkanFramebuffer> framebuffer =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanFramebuffer>( m_Specification.Framebuffer );

        VkGraphicsPipelineCreateInfo pipelineInfo = {
             .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
             .stageCount          = (uint32_t)vulkanShader->GetPipelineShaderStageCreateInfos().size(),
             .pStages             = vulkanShader->GetPipelineShaderStageCreateInfos().data(),
             .pVertexInputState   = &vertexInputInfo,
             .pInputAssemblyState = &pipelineIACreateInfo,
             .pViewportState      = &VPCreateInfo,
             .pRasterizationState = &rastCreateInfo,
             .pMultisampleState   = &pipelineMSCreateInfo,
             .pColorBlendState    = &BlendCreateInfo,
             .layout              = m_PipelineLayout,
             .renderPass          = framebuffer->GetRenderPass(),
             .subpass             = 0,
             .basePipelineHandle  = VK_NULL_HANDLE,
             .basePipelineIndex   = -1 };

        res = vkCreateGraphicsPipelines( device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &m_Pipeline );

        LOG_INFO( "Created {} VulkanPipeline", m_Specification.DebugName );
    }

} // namespace Desert::Graphic::API::Vulkan
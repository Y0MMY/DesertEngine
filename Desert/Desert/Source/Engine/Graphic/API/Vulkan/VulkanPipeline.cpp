#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <Engine/Graphic/VertexBuffer.hpp>

namespace Desert::Graphic::API::Vulkan
{

    namespace
    {
        static VkPolygonMode ConvertVkPolygonMode( PrimitivePolygonMode mode )
        {
            switch ( mode )
            {
                case PrimitivePolygonMode::Solid:
                    return VK_POLYGON_MODE_FILL;
                case PrimitivePolygonMode::Wireframe:
                    return VK_POLYGON_MODE_LINE;
            }

            return VK_POLYGON_MODE_FILL;
        }

        static VkStencilOp ConvertStencilOp( StencilOp op )
        {
            switch ( op )
            {
                case StencilOp::Keep:
                    return VK_STENCIL_OP_KEEP;
                case StencilOp::Zero:
                    return VK_STENCIL_OP_ZERO;
                case StencilOp::Replace:
                    return VK_STENCIL_OP_REPLACE;
                case StencilOp::IncrementAndClamp:
                    return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
                case StencilOp::DecrementAndClamp:
                    return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
                case StencilOp::Invert:
                    return VK_STENCIL_OP_INVERT;
                case StencilOp::IncrementAndWrap:
                    return VK_STENCIL_OP_INCREMENT_AND_WRAP;
                case StencilOp::DecrementAndWrap:
                    return VK_STENCIL_OP_DECREMENT_AND_WRAP;
                default:
                    return VK_STENCIL_OP_KEEP;
            }
        }

        static VkCompareOp ConvertCompareOp( CompareOp op )
        {
            switch ( op )
            {
                case CompareOp::Never:
                    return VK_COMPARE_OP_NEVER;
                case CompareOp::Less:
                    return VK_COMPARE_OP_LESS;
                case CompareOp::Equal:
                    return VK_COMPARE_OP_EQUAL;
                case CompareOp::LessOrEqual:
                    return VK_COMPARE_OP_LESS_OR_EQUAL;
                case CompareOp::Greater:
                    return VK_COMPARE_OP_GREATER;
                case CompareOp::NotEqual:
                    return VK_COMPARE_OP_NOT_EQUAL;
                case CompareOp::GreaterOrEqual:
                    return VK_COMPARE_OP_GREATER_OR_EQUAL;
                case CompareOp::Always:
                    return VK_COMPARE_OP_ALWAYS;
                default:
                    return VK_COMPARE_OP_ALWAYS;
            }
        }

        static VkCullModeFlags ConvertCullMode( CullMode mode )
        {
            switch ( mode )
            {
                case CullMode::None:
                    return VK_CULL_MODE_NONE;
                case CullMode::Front:
                    return VK_CULL_MODE_FRONT_BIT;
                case CullMode::Back:
                    return VK_CULL_MODE_BACK_BIT;
                case CullMode::FrontAndBack:
                    return VK_CULL_MODE_FRONT_AND_BACK;
                default:
                    return VK_CULL_MODE_BACK_BIT;
            }
        }

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

        const auto vertexInputSize = vertexInputBinding.stride > 0 ? 1U : 0U;

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
             .vertexBindingDescriptionCount   = vertexInputSize,
             .pVertexBindingDescriptions      = &vertexInputBinding,
             .vertexAttributeDescriptionCount = (uint32_t)vertexInputAttributes.size(),
             .pVertexAttributeDescriptions    = vertexInputAttributes.data() };

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
             .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
             .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
             .primitiveRestartEnable = VK_FALSE };

        std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                      VK_DYNAMIC_STATE_LINE_WIDTH };

        VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
        dynamicStateInfo.sType                            = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateInfo.dynamicStateCount                = static_cast<uint32_t>( dynamicStates.size() );
        dynamicStateInfo.pDynamicStates                   = dynamicStates.data();

        VkPipelineViewportStateCreateInfo viewportState = {
             .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
             .viewportCount = 1,
             .pViewports    = nullptr,
             .scissorCount  = 1,
        };

        VkPipelineRasterizationStateCreateInfo rasterizer = {
             .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
             .depthClampEnable        = VK_FALSE,
             .rasterizerDiscardEnable = VK_FALSE,
             .polygonMode             = ConvertVkPolygonMode(m_Specification.PolygonMode),

             .cullMode                = ConvertCullMode( m_Specification.CullMode ),
             .frontFace               = VK_FRONT_FACE_CLOCKWISE,
             .depthBiasEnable         = VK_FALSE,
             .lineWidth               = 1.0F };

        VkPipelineMultisampleStateCreateInfo multisampling = {
             .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
             .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
             .sampleShadingEnable  = VK_FALSE,
        };

        const auto hasDepth = HasDepth();

        VkPipelineDepthStencilStateCreateInfo depthStencil = {
             .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
             .depthTestEnable       = m_Specification.DepthTestEnabled ? VK_TRUE : VK_FALSE,
             .depthWriteEnable      = m_Specification.DepthWriteEnabled ? VK_TRUE : VK_FALSE,
             .depthCompareOp        = ConvertCompareOp( m_Specification.DepthCompareOp ),
             .depthBoundsTestEnable = VK_FALSE,
             .stencilTestEnable     = m_Specification.StencilTestEnabled ? VK_TRUE : VK_FALSE,
             .front                 = { .failOp      = ConvertStencilOp( m_Specification.StencilFront.FailOp ),
                                        .passOp      = ConvertStencilOp( m_Specification.StencilFront.PassOp ),
                                        .depthFailOp = ConvertStencilOp( m_Specification.StencilFront.DepthFailOp ),
                                        .compareOp   = ConvertCompareOp( m_Specification.StencilFront.CompareOp ),
                                        .compareMask = m_Specification.StencilFront.CompareMask,
                                        .writeMask   = m_Specification.StencilFront.WriteMask,
                                        .reference   = m_Specification.StencilFront.Reference },
             .back                  = { .failOp      = ConvertStencilOp( m_Specification.StencilBack.FailOp ),
                                        .passOp      = ConvertStencilOp( m_Specification.StencilBack.PassOp ),
                                        .depthFailOp = ConvertStencilOp( m_Specification.StencilBack.DepthFailOp ),
                                        .compareOp   = ConvertCompareOp( m_Specification.StencilBack.CompareOp ),
                                        .compareMask = m_Specification.StencilBack.CompareMask,
                                        .writeMask   = m_Specification.StencilBack.WriteMask,
                                        .reference   = m_Specification.StencilBack.Reference },
             .minDepthBounds        = 0.0f,
             .maxDepthBounds        = 0.0f };

        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
        colorBlendAttachments.resize( m_Specification.Framebuffer->GetColorAttachmentCount() );
        for ( auto& attachment : colorBlendAttachments )
        {
            attachment.blendEnable    = VK_FALSE;
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }

        VkPipelineColorBlendStateCreateInfo colorBlending = {
             .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
             .logicOpEnable   = VK_FALSE,
             .logicOp         = VK_LOGIC_OP_CLEAR,
             .attachmentCount = static_cast<uint32_t>( colorBlendAttachments.size() ),
             .pAttachments    = colorBlendAttachments.data(),
             .blendConstants  = { 0.0f, 0.0f, 0.0f, 0.0f },
        };

        VulkanShader* vulkanShader =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( m_Specification.Shader ).get();

        const auto descriptorSets       = vulkanShader->GetShaderDescriptorSets();
        const auto descriptorSetLayouts = vulkanShader->GetAllDescriptorSetLayouts();

        const auto& pushConstant = SetUpPushConstantRange();

        VkPipelineLayoutCreateInfo lyoutInfo = { .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .setLayoutCount = (uint32_t)descriptorSetLayouts.size(),
                                                 .pSetLayouts    = descriptorSetLayouts.data(),
                                                 .pushConstantRangeCount = pushConstant.first,
                                                 .pPushConstantRanges    = &pushConstant.second };

        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        VkResult res = vkCreatePipelineLayout( device, &lyoutInfo, VK_NULL_HANDLE, &m_PipelineLayout );

        if ( !m_Specification.Framebuffer )
        {
            // return TODO: assert
        }

        const auto& renderPass =
             sp_cast<API::Vulkan::VulkanFramebuffer>( m_Specification.Framebuffer )->GetVKRenderPass();

        VkGraphicsPipelineCreateInfo pipelineInfo = {
             .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
             .stageCount          = (uint32_t)vulkanShader->GetPipelineShaderStageCreateInfos().size(),
             .pStages             = vulkanShader->GetPipelineShaderStageCreateInfos().data(),
             .pVertexInputState   = &vertexInputInfo,
             .pInputAssemblyState = &inputAssembly,
             .pViewportState      = &viewportState,
             .pRasterizationState = &rasterizer,
             .pMultisampleState   = &multisampling,
             .pDepthStencilState  = &depthStencil,
             .pColorBlendState    = &colorBlending,
             .pDynamicState       = &dynamicStateInfo,
             .layout              = m_PipelineLayout,
             .renderPass          = renderPass,
             .subpass             = 0,
             .basePipelineHandle  = VK_NULL_HANDLE,
             .basePipelineIndex   = -1 };

        res = vkCreateGraphicsPipelines( device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &m_Pipeline );

        LOG_INFO( "Created {} VulkanPipeline", m_Specification.DebugName );
    } // namespace Desert::Graphic::API::Vulkan

    std::pair<uint32_t, VkPushConstantRange> VulkanPipeline::SetUpPushConstantRange() const
    {
        VulkanShader* vulkanShader =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( m_Specification.Shader ).get();

        const auto& pushConstant = vulkanShader->GetShaderPushConstant();
        if ( !pushConstant )
        {
            return { 0, {} };
        }

        const auto& pcValue = pushConstant.value();
        // setup push constants
        VkPushConstantRange pushConstantCI;
        pushConstantCI.offset = pcValue.Offset;
        pushConstantCI.size   = pcValue.Size;

        switch ( pcValue.ShaderStage ) // TODO
        {
            case Core::Formats::ShaderStage::Vertex:
            {
                pushConstantCI.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            }

            case Core::Formats::ShaderStage::Fragment:
            {
                pushConstantCI.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            }

            case Core::Formats::ShaderStage::Compute:
            {
                pushConstantCI.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                break;
            }
        }

        return { 1, pushConstantCI };
    }

    bool VulkanPipeline::HasDepth()
    {
        const auto& attachments = m_Specification.Framebuffer->GetSpecification().Attachments.Attachments;
        return std::any_of( attachments.begin(), attachments.end(),
                            []( const auto& attachment ) { return Utils::IsDepthFormat( attachment ); } );
    }

} // namespace Desert::Graphic::API::Vulkan
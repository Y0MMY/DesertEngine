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

    void VulkanPipeline::CreatePipelineLayout()
    {
        VulkanShader* vulkanShader =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( m_Specification.Shader ).get();

        const auto  descriptorSetLayouts = vulkanShader->GetAllDescriptorSetLayouts();
        const auto& pushConstant         = SetUpPushConstantRange();

        VkPipelineLayoutCreateInfo layoutInfo = {
             .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
             .setLayoutCount         = static_cast<uint32_t>( descriptorSetLayouts.size() ),
             .pSetLayouts            = descriptorSetLayouts.data(),
             .pushConstantRangeCount = pushConstant.first,
             .pPushConstantRanges    = pushConstant.first > 0 ? &pushConstant.second : nullptr };

        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        VkResult result = vkCreatePipelineLayout( device, &layoutInfo, nullptr, &m_PipelineLayout );

        if ( result != VK_SUCCESS )
        {
            throw std::runtime_error( "Failed to create pipeline layout" );
        }
    }

    void VulkanPipeline::CreateVertexInputState()
    {
        if ( m_Specification.PullingConfig )
        {
            m_VertexInputInfo = VkPipelineVertexInputStateCreateInfo{
                 .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                 .vertexBindingDescriptionCount   = 0,
                 .pVertexBindingDescriptions      = nullptr,
                 .vertexAttributeDescriptionCount = 0,
                 .pVertexAttributeDescriptions    = nullptr };
            return;
        }

        if ( !m_Specification.Layout || m_Specification.Layout->GetElementCount() == 0 )
        {
            m_VertexInputInfo = VkPipelineVertexInputStateCreateInfo{
                 .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                 .vertexBindingDescriptionCount   = 0,
                 .pVertexBindingDescriptions      = nullptr,
                 .vertexAttributeDescriptionCount = 0,
                 .pVertexAttributeDescriptions    = nullptr };
            return;
        }

        m_VertexInputBinding = VkVertexInputBindingDescription{ .binding   = 0,
                                                                .stride    = m_Specification.Layout->GetStride(),
                                                                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };

        m_VertexAttributes.clear();
        const auto& layout = m_Specification.Layout.value();
        for ( uint32_t location = 0; const auto& element : layout )
        {
            m_VertexAttributes.push_back(
                 VkVertexInputAttributeDescription{ .location = location,
                                                    .binding  = 0,
                                                    .format   = ShaderDataTypeToVulkanFormat( element.Type ),
                                                    .offset   = element.Offset } );
            location++;
        }

        m_VertexInputInfo = VkPipelineVertexInputStateCreateInfo{
             .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
             .vertexBindingDescriptionCount   = 1,
             .pVertexBindingDescriptions      = &m_VertexInputBinding,
             .vertexAttributeDescriptionCount = static_cast<uint32_t>( m_VertexAttributes.size() ),
             .pVertexAttributeDescriptions    = m_VertexAttributes.data() };
    }

    void VulkanPipeline::CreateInputAssemblyState()
    {
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        switch ( m_Specification.Topology )
        {
            case PrimitiveTopology::Points:
                topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                break;
            case PrimitiveTopology::Lines:
                topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                break;
            case PrimitiveTopology::LineStrip:
                topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                break;
            case PrimitiveTopology::Triangles:
                topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                break;
            case PrimitiveTopology::TriangleStrip:
                topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                break;
        }

        m_InputAssembly = VkPipelineInputAssemblyStateCreateInfo{
             .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
             .topology               = topology,
             .primitiveRestartEnable = VK_FALSE };
    }

    void VulkanPipeline::CreateDynamicState()
    {
        m_DynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH };

        m_DynamicStateInfo = VkPipelineDynamicStateCreateInfo{
             .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
             .dynamicStateCount = static_cast<uint32_t>( m_DynamicStates.size() ),
             .pDynamicStates    = m_DynamicStates.data() };
    }

    void VulkanPipeline::CreateViewportState()
    {
        m_ViewportState =
             VkPipelineViewportStateCreateInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                .viewportCount = 1,
                                                .pViewports    = nullptr,
                                                .scissorCount  = 1,
                                                .pScissors     = nullptr };
    }

    void VulkanPipeline::CreateRasterizationState()
    {
        m_Rasterizer = VkPipelineRasterizationStateCreateInfo{
             .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
             .depthClampEnable        = VK_FALSE,
             .rasterizerDiscardEnable = VK_FALSE,
             .polygonMode             = ConvertVkPolygonMode( m_Specification.PolygonMode ),
             .cullMode                = ConvertCullMode( m_Specification.CullMode ),
             .frontFace               = VK_FRONT_FACE_CLOCKWISE,
             .depthBiasEnable         = VK_FALSE,
             .lineWidth               = m_Specification.LineWidth };
    }

    void VulkanPipeline::CreateMultisampleState()
    {
        m_Multisampling = VkPipelineMultisampleStateCreateInfo{
             .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
             .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
             .sampleShadingEnable  = VK_FALSE };
    }

    void VulkanPipeline::CreateDepthStencilState()
    {
        VkStencilOpState frontStencil = ConvertStencilOpState( m_Specification.StencilFront );
        VkStencilOpState backStencil  = ConvertStencilOpState( m_Specification.StencilBack );

        m_DepthStencil = VkPipelineDepthStencilStateCreateInfo{
             .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
             .depthTestEnable       = m_Specification.DepthTestEnabled ? VK_TRUE : VK_FALSE,
             .depthWriteEnable      = m_Specification.DepthWriteEnabled ? VK_TRUE : VK_FALSE,
             .depthCompareOp        = ConvertCompareOp( m_Specification.DepthCompareOp ),
             .depthBoundsTestEnable = VK_FALSE,
             .stencilTestEnable     = m_Specification.StencilTestEnabled ? VK_TRUE : VK_FALSE,
             .front                 = frontStencil,
             .back                  = backStencil,
             .minDepthBounds        = 0.0f,
             .maxDepthBounds        = 1.0f };
    }

    void VulkanPipeline::CreateColorBlendState()
    {
        m_ColorBlendAttachments.clear();
        uint32_t colorAttachmentCount =
             m_Specification.Framebuffer ? m_Specification.Framebuffer->GetColorAttachmentCount() : 1;

        m_ColorBlendAttachments.resize( colorAttachmentCount );
        for ( auto& attachment : m_ColorBlendAttachments )
        {
            attachment = { .blendEnable    = VK_FALSE,
                           .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
        }

        m_ColorBlending = VkPipelineColorBlendStateCreateInfo{
             .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
             .logicOpEnable   = VK_FALSE,
             .logicOp         = VK_LOGIC_OP_COPY,
             .attachmentCount = static_cast<uint32_t>( m_ColorBlendAttachments.size() ),
             .pAttachments    = m_ColorBlendAttachments.data(),
             .blendConstants  = { 0.0f, 0.0f, 0.0f, 0.0f } };
    }

    void VulkanPipeline::CreateGraphicsPipeline( VkDevice device, VulkanShader* vulkanShader )
    {
        if ( !m_Specification.Framebuffer )
        {
            throw std::runtime_error( "Framebuffer is required for pipeline creation" );
        }

        VkRenderPass renderPass =
             std::static_pointer_cast<API::Vulkan::VulkanFramebuffer>( m_Specification.Framebuffer )
                  ->GetVKRenderPass();

        VkGraphicsPipelineCreateInfo pipelineInfo = {
             .sType      = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
             .stageCount = static_cast<uint32_t>( vulkanShader->GetPipelineShaderStageCreateInfos().size() ),
             .pStages    = vulkanShader->GetPipelineShaderStageCreateInfos().data(),
             .pVertexInputState   = &m_VertexInputInfo,
             .pInputAssemblyState = &m_InputAssembly,
             .pViewportState      = &m_ViewportState,
             .pRasterizationState = &m_Rasterizer,
             .pMultisampleState   = &m_Multisampling,
             .pDepthStencilState  = &m_DepthStencil,
             .pColorBlendState    = &m_ColorBlending,
             .pDynamicState       = &m_DynamicStateInfo,
             .layout              = m_PipelineLayout,
             .renderPass          = renderPass,
             .subpass             = 0,
             .basePipelineHandle  = VK_NULL_HANDLE,
             .basePipelineIndex   = -1 };

        VkResult result =
             vkCreateGraphicsPipelines( device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline );

        if ( result != VK_SUCCESS )
        {
            throw std::runtime_error( "Failed to create graphics pipeline" );
        }
    }

    void VulkanPipeline::Invalidate()
    {
        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        if ( m_Pipeline != VK_NULL_HANDLE )
        {
            vkDestroyPipeline( device, m_Pipeline, nullptr );
            m_Pipeline = VK_NULL_HANDLE;
        }
        if ( m_PipelineLayout != VK_NULL_HANDLE )
        {
            vkDestroyPipelineLayout( device, m_PipelineLayout, nullptr );
            m_PipelineLayout = VK_NULL_HANDLE;
        }

        CreatePipelineLayout();

        CreateVertexInputState();
        CreateInputAssemblyState();
        CreateDynamicState();
        CreateViewportState();
        CreateRasterizationState();
        CreateMultisampleState();
        CreateDepthStencilState();
        CreateColorBlendState();

        VulkanShader* vulkanShader =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( m_Specification.Shader ).get();

        CreateGraphicsPipeline( device, vulkanShader );

        LOG_INFO( "Created {} VulkanPipeline", m_Specification.DebugName );
    }

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

    VkStencilOpState VulkanPipeline::ConvertStencilOpState( const StencilOpState& state )
    {
        return VkStencilOpState{ .failOp      = ConvertStencilOp( state.FailOp ),
                                 .passOp      = ConvertStencilOp( state.PassOp ),
                                 .depthFailOp = ConvertStencilOp( state.DepthFailOp ),
                                 .compareOp   = ConvertCompareOp( state.CompareOp ),
                                 .compareMask = state.CompareMask,
                                 .writeMask   = state.WriteMask,
                                 .reference   = state.Reference };
    }

    VulkanPipeline::~VulkanPipeline()
    {
    }

} // namespace Desert::Graphic::API::Vulkan
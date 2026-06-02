#pragma once

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Vertexbuffer.hpp>

#include <Common/Core/Memory/Buffer.hpp>

namespace Desert::Graphic
{
    enum class PipelineType
    {
        Graphics,
        Compute
    };

    /**
     * @brief Base interface for all pipeline states.
     */
    class IPipeline
    {
    public:
        virtual ~IPipeline() = default;

        virtual void Invalidate() = 0;
        virtual void Release()    = 0;

        [[nodiscard]] virtual PipelineType GetType() const = 0;
        [[nodiscard]] virtual std::shared_ptr<Shader> GetShader() const = 0;
    };

    // --- Graphics Pipeline ---

    enum class StencilOp
    {
        Keep = 0, Zero, Replace, IncrementAndClamp, DecrementAndClamp, Invert, IncrementAndWrap, DecrementAndWrap
    };

    enum class CompareOp
    {
        Never = 0, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always
    };

    enum class CullMode
    {
        None = 0, Front, Back, FrontAndBack
    };

    struct StencilOpState
    {
        StencilOp FailOp;
        StencilOp PassOp;
        StencilOp DepthFailOp;
        CompareOp CompareOp;
        uint32_t  CompareMask;
        uint32_t  WriteMask;
        uint32_t  Reference;
    };

    enum class PrimitiveTopology
    {
        Points = 0, Lines, Triangles, LineStrip, TriangleStrip, TriangleFan
    };

    inline bool PrimitiveIsLine( PrimitiveTopology topology )
    {
        return topology == PrimitiveTopology::Lines || topology == PrimitiveTopology::LineStrip;
    }

    enum class PrimitivePolygonMode
    {
        Solid = 0, Wireframe
    };

    struct VertexPullingAttribute
    {
        ShaderDataType Type;
        std::string    Name;
        uint32_t       Offset;
        uint32_t       BindingPoint;
    };

    struct VertexPullingConfig
    {
        uint32_t                            VertexStride = 0;
        std::vector<VertexPullingAttribute> Attributes;
        BufferUsage                         Usage = BufferUsage::Dynamic;
    };

    struct GraphicsPipelineSpecification
    {
        std::shared_ptr<Shader>            Shader;
        std::shared_ptr<Framebuffer>       Framebuffer;
        std::shared_ptr<RenderPass>        Renderpass;
        std::optional<VertexBufferLayout>  Layout;
        std::optional<VertexPullingConfig> PullingConfig;

        bool           DepthTestEnabled   = true;
        CompareOp      DepthCompareOp     = CompareOp::Less;
        bool           StencilTestEnabled = false;
        StencilOpState StencilFront;
        StencilOpState StencilBack;
        CullMode       CullMode          = CullMode::None;
        bool           DepthWriteEnabled = true;

        float                LineWidth   = 1.0F;
        PrimitiveTopology    Topology    = PrimitiveTopology::Triangles;
        PrimitivePolygonMode PolygonMode = PrimitivePolygonMode::Solid;

        std::string DebugName;
    };

    class GraphicsPipeline : public IPipeline
    {
    public:
        [[nodiscard]] virtual const GraphicsPipelineSpecification& GetSpecification() const = 0;
        
        static std::shared_ptr<GraphicsPipeline> Create( const GraphicsPipelineSpecification& spec );
    };

    // --- Compute Pipeline ---

    struct ComputePipelineSpecification
    {
        std::shared_ptr<Shader> Shader;
        std::string             DebugName;
    };

    class ComputePipeline : public IPipeline
    {
    public:
        [[nodiscard]] virtual const ComputePipelineSpecification& GetSpecification() const = 0;
        
        /**
         * @brief Updates the internal storage buffer (if any) associated with this pipeline.
         */
        virtual void UpdateStorageBuffer( void* data, std::size_t size ) = 0;

        static std::shared_ptr<ComputePipeline> Create( const ComputePipelineSpecification& spec );
    };

} // namespace Desert::Graphic

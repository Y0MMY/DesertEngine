#pragma once

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Vertexbuffer.hpp>

#include <Common/Core/Memory/Buffer.hpp>

namespace Desert::ShaderResources
{
    class StorageBuffer;
}

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
        [[nodiscard]] virtual const std::shared_ptr<Shader>& GetShader() const = 0;
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

    enum class BlendFactor
    {
        Zero = 0, One, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor,
        SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha
    };

    struct StencilOpState
    {
        StencilOp FailOp      = StencilOp::Keep;
        StencilOp PassOp      = StencilOp::Keep;
        StencilOp DepthFailOp = StencilOp::Keep;
        CompareOp CompareOp   = CompareOp::Always;
        uint32_t  CompareMask = 0xFF;
        uint32_t  WriteMask   = 0xFF;
        uint32_t  Reference   = 0;
    };

    enum class PrimitiveTopology
    {
        Points = 0, Lines, Triangles, LineStrip, TriangleStrip, TriangleFan, Patches
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
        // Standard src-alpha / one-minus-src-alpha blending (transparency overlays, e.g. the scene grid).
        bool           BlendEnable       = false;
        // Color blend factors used when BlendEnable is on. Defaults reproduce the previous hardcoded alpha
        // blend, so shaders that don't declare custom factors render identically. Alpha channel mirrors these.
        BlendFactor    SrcColorBlendFactor = BlendFactor::SrcAlpha;
        BlendFactor    DstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;

        // Build the pipeline against the target framebuffer's LOAD render pass instead of the CLEAR one, so
        // the pass can be begun with clearFrame=false (preserve existing content) without a render-pass
        // incompatibility. Used by the deferred lighting pass to composite over the forward-rendered scene.
        bool           UseLoadRenderPass = false;

        float                LineWidth   = 1.0F;
        PrimitiveTopology    Topology    = PrimitiveTopology::Triangles;
        PrimitivePolygonMode PolygonMode = PrimitivePolygonMode::Solid;

        // > 0 enables tessellation: topology becomes a patch list with this many control points per patch
        // (the pipeline must have tessellation control + evaluation stages).
        uint32_t PatchControlPoints = 0;

        std::string DebugName;
    };

    class GraphicsPipeline : public IPipeline
    {
    public:
        [[nodiscard]] virtual const GraphicsPipelineSpecification& GetSpecification() const = 0;
        
        static std::shared_ptr<GraphicsPipeline> Create( const GraphicsPipelineSpecification& spec );
    };

    // --- Compute Pipeline ---

    // ONE SPECIALIZATION CONSTANT: a value the SPIR-V declares with `layout(constant_id = Id)` and the
    // DRIVER substitutes when the pipeline is created, before it compiles the shader for the device. It is
    // the engine's whole answer to shader permutation — one SPIR-V module, one cache entry, N pipelines —
    // and it costs nothing at the call site because the value was already known on the CPU.
    //
    // What it buys that a uniform read cannot: a branch on a specialization constant is FOLDED, and the
    // branch it deletes takes its stores with it. The cloud raymarch is the case that asked for this — a
    // one-layer sky was paying for a two-layer loop it never entered, because a store to the layer index in
    // the branch it did not take is still a store the optimiser has to assume, and it turns ~50 hoistable
    // parameter loads per density sample into indexed reads inside the march (Common/CloudParams.glslh has
    // the four measured alternatives).
    //
    // INT32 ONLY, deliberately. Vulkan allows any scalar, but every specialization this engine has is a
    // count or a mode; a variant type here would be machinery in front of one `int`.
    struct ShaderSpecializationConstant
    {
        uint32_t Id    = 0;
        int32_t  Value = 0;
    };

    struct ComputePipelineSpecification
    {
        std::shared_ptr<Shader> Shader;
        std::string             DebugName;

        // Substituted into the shader at pipeline creation. Empty means the SPIR-V's own defaults, which
        // is what every pass but the cloud raymarch wants.
        std::vector<ShaderSpecializationConstant> Specialization;
    };

    class ComputePipeline : public IPipeline
    {
    public:
        [[nodiscard]] virtual const ComputePipelineSpecification& GetSpecification() const = 0;

        // --- Resource-binding API (UE-style): set inputs/outputs/push-constants, then Dispatch ---

        /** Bind a sampled input image at @p binding (e.g. a panorama or a source cubemap). */
        virtual ComputePipeline& SetInput( uint32_t binding, Image* image ) = 0;
        /** Bind a writable storage output image at @p binding; @p mip selects the target mip view. */
        virtual ComputePipeline& SetOutput( uint32_t binding, Image* image, uint32_t mip = 0 ) = 0;
        /** Bind a read-write storage buffer at @p binding (e.g. a luminance histogram). */
        virtual ComputePipeline& SetStorageBuffer( uint32_t binding, ShaderResources::StorageBuffer* buffer ) = 0;
        /** Set the raw push-constant block used by the next Dispatch (e.g. prefilter roughness). */
        virtual ComputePipeline& SetPushConstants( const void* data, uint32_t size ) = 0;
        /** Record + submit one immediate compute dispatch with the currently-bound resources. */
        virtual void Dispatch( uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ ) = 0;

        [[nodiscard]] virtual Image* GetInput( uint32_t binding ) const  = 0;
        [[nodiscard]] virtual Image* GetOutput( uint32_t binding ) const = 0;

        static std::shared_ptr<ComputePipeline> Create( const ComputePipelineSpecification& spec );
    };

} // namespace Desert::Graphic

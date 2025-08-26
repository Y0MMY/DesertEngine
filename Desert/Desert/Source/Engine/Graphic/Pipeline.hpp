#pragma once

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Vertexbuffer.hpp>

#include <Common/Core/Memory/Buffer.hpp>

namespace Desert::Graphic
{
    enum class StencilOp
    {
        Keep = 0,
        Zero,
        Replace,
        IncrementAndClamp,
        DecrementAndClamp,
        Invert,
        IncrementAndWrap,
        DecrementAndWrap
    };

    enum class CompareOp
    {
        Never = 0,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always
    };

    enum class CullMode
    {
        None = 0,
        Front,
        Back,
        FrontAndBack
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
        Points = 0,
        Lines,
        Triangles,
        LineStrip,
        TriangleStrip,
        TriangleFan
    };

    inline bool PrimitiveIsLine( PrimitiveTopology topology )
    {
        if ( topology == PrimitiveTopology::Lines || topology == PrimitiveTopology::LineStrip )
        {
            return true;
        }

        return false;
    }

    enum class PrimitivePolygonMode
    {
        Solid = 0,
        Wireframe,
    };

    struct PipelineSpecification
    {
        std::shared_ptr<Shader>      Shader;
        std::shared_ptr<Framebuffer> Framebuffer;
        std::shared_ptr<RenderPass>  Renderpass;
        VertexBufferLayout           Layout;

        bool           DepthTestEnabled   = true;
        CompareOp      DepthCompareOp     = CompareOp::Less;
        bool           StencilTestEnabled = false;
        StencilOpState StencilFront;
        StencilOpState StencilBack;
        CullMode       CullMode          = CullMode::None;
        bool           DepthWriteEnabled = true;

        float                LineWidth   = 1.0F; // Also affects the PrimitivePolygonMode::Wireframe
        PrimitiveTopology    Topology    = PrimitiveTopology::Triangles;
        PrimitivePolygonMode PolygonMode = PrimitivePolygonMode::Solid;

        std::string DebugName;
    };

    class Pipeline
    {
    public:
        virtual ~Pipeline() = default;

        virtual const PipelineSpecification GetSpecification() const = 0;

        virtual void Invalidate() = 0;

        static std::shared_ptr<Pipeline> Create( const PipelineSpecification& spec );
    };

    class PipelineCompute
    {
    public:
        virtual ~PipelineCompute() = default;

        virtual void Begin()                                                                             = 0;
        virtual void Execute( const std::shared_ptr<Image>& imageForProccess, std::shared_ptr<Image>& outputImage,
                              uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ )         = 0;
        virtual void ExecuteMipLevel( const std::shared_ptr<Image>& imageForProccess, uint32_t mipLevel,
                                      uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ ) = 0;
        virtual void Dispatch( uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ )        = 0;
        virtual void End()                                                                               = 0;

        virtual void UpdateStorageBuffer( void* data, std::size_t size ) final;

        virtual void Invalidate() = 0;
        virtual void Release()    = 0;

        static std::shared_ptr<PipelineCompute> Create( const std::shared_ptr<Shader>& shader );

    protected:
        Common::Memory::Buffer m_StorageBuffer; // 128 bytes
    };

} // namespace Desert::Graphic
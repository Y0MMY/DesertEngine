#include "Render2D.hpp"

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>
#include <Engine/Graphic/IndexBuffer.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Properties/Texture2DProperty.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/Shader/ShaderService.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace Desert::Graphic::Render2D
{
    Common::BoolResultStr Render2D::Init( const std::shared_ptr<Framebuffer>& target )
    {
        if ( !target )
            return Common::MakeError( "Render2D::Init: null target framebuffer" );

        auto* shaderService = Runtime::ResourceRegistry::GetShaderService();
        if ( !shaderService )
            return Common::MakeError( "Render2D::Init: no shader service" );

        m_Shader = shaderService->GetByName( "UI2D" );
        if ( !m_Shader )
            return Common::MakeError( "Render2D::Init: missing shader 'UI2D'" );

        // Pixel-space quads: pos(vec2) + uv(vec2) + straight RGBA(vec4). Matches DrawList2D::Vertex2D.
        const VertexBufferLayout layout( { VertexBufferElement( ShaderDataType::Float2, "a_Position" ),
                                           VertexBufferElement( ShaderDataType::Float2, "a_TexCoord" ),
                                           VertexBufferElement( ShaderDataType::Float4, "a_Color" ) } );

        GraphicsPipelineSpecification spec;
        spec.DebugName          = "UI2DPipeline";
        spec.Shader             = m_Shader;
        spec.Framebuffer        = target;
        spec.Layout             = layout;
        spec.Topology           = PrimitiveTopology::Triangles;
        spec.CullMode           = CullMode::None;
        spec.DepthTestEnabled   = false; // UI is a flat overlay — no depth
        spec.DepthWriteEnabled  = false;
        spec.BlendEnable        = true; // straight-alpha composite over the scene
        spec.UseLoadRenderPass  = true; // draw ON TOP of the composited scene, don't clear it

        m_Pipeline = GraphicsPipeline::Create( spec );
        if ( !m_Pipeline )
            return Common::MakeError( "Render2D::Init: failed to create UI2D pipeline" );
        m_Pipeline->Invalidate();

        // 1x1 white texture so solid shapes collapse to their vertex colour (texture * colour == colour).
        // Created once and reused; the pipeline is rebuilt every Init but the texture/buffers persist.
        if ( !m_WhiteTexture )
        {
            const unsigned char       whitePixel[4] = { 255, 255, 255, 255 };
            Core::Formats::ImagePixelData data =
                 std::vector<unsigned char>( whitePixel, whitePixel + 4 );

            TextureSpecification texSpec;
            texSpec.GenerateMips = false;
            auto texResult       = Texture2D::Create( texSpec, "Render2D_White", 1, 1,
                                                      Core::Formats::ImageFormat::RGBA8F, std::move( data ) );
            if ( !texResult )
                return Common::MakeError( "Render2D::Init: failed to create white texture" );
            m_WhiteTexture = texResult.ExtractValue();

            if ( auto* imgService = Runtime::ResourceRegistry::GetImageService() )
                m_WhiteImage = static_cast<Image2D*>( imgService->Resolve( m_WhiteTexture->GetImageHandle() ) );
        }

        return Common::MakeSuccess( true );
    }

    void Render2D::BeginFrame( const glm::vec4& viewportPx )
    {
        // Pixel -> clip. Top-left origin, y down: the engine uses a negative-height viewport (GL-style
        // Y-up NDC), so mapping bottom=y+h to NDC -1 and top=y to NDC +1 lands the origin at the top-left.
        m_Projection = glm::ortho( viewportPx.x, viewportPx.x + viewportPx.z, viewportPx.y + viewportPx.w,
                                   viewportPx.y );
        m_DrawList.Reset();
    }

    void Render2D::EnsureCapacity( uint32_t vertexCount, uint32_t indexCount )
    {
        if ( vertexCount > m_VertexCapacity )
        {
            const uint32_t cap = std::max( vertexCount, m_VertexCapacity ? m_VertexCapacity * 2 : 4096u );
            m_VertexBuffer     = VertexBuffer::Create( cap * (uint32_t)sizeof( Vertex2D ), BufferUsage::Dynamic );
            m_VertexBuffer->Invalidate(); // Create() only constructs; Invalidate() allocates GPU memory
            m_VertexCapacity = cap;
        }
        if ( indexCount > m_IndexCapacity )
        {
            const uint32_t cap = std::max( indexCount, m_IndexCapacity ? m_IndexCapacity * 2 : 8192u );
            m_IndexBuffer      = IndexBuffer::Create( cap * (uint32_t)sizeof( uint32_t ), BufferUsage::Dynamic );
            m_IndexBuffer->Invalidate();
            m_IndexCapacity = cap;
        }
    }

    MaterialExecutor* Render2D::ExecutorForTexture( const void* texture )
    {
        auto it = m_Executors.find( texture );
        if ( it != m_Executors.end() )
            return it->second.get();

        auto exec = MaterialExecutor::Create( "Render2D_UI2D", m_Shader );
        auto* raw = exec.get();
        m_Executors.emplace( texture, std::move( exec ) );
        return raw;
    }

    void Render2D::Flush()
    {
        if ( !m_Pipeline || m_DrawList.Empty() )
            return;

        const auto& verts = m_DrawList.GetVertices();
        const auto& idx   = m_DrawList.GetIndices();

        EnsureCapacity( (uint32_t)verts.size(), (uint32_t)idx.size() );
        m_VertexBuffer->SetData( (void*)verts.data(), (uint32_t)( verts.size() * sizeof( Vertex2D ) ), 0 );
        m_IndexBuffer->SetData( (void*)idx.data(), (uint32_t)( idx.size() * sizeof( uint32_t ) ), 0 );

        auto& renderer = Renderer::GetInstance();
        for ( const auto& cmd : m_DrawList.GetCommands() )
        {
            if ( cmd.IndexCount == 0 )
                continue;

            MaterialExecutor* exec = ExecutorForTexture( cmd.Texture );
            if ( !exec )
                continue;

            const Image2D* img = cmd.Texture ? static_cast<const Image2D*>( cmd.Texture ) : m_WhiteImage;
            if ( auto texProp = exec->GetTexture2DProperty( "u_Texture" ) )
                texProp->SetImage( img );
            exec->PushConstant( &m_Projection, (uint32_t)sizeof( glm::mat4 ) );

            renderer.SubmitIndexed( m_Pipeline.get(), m_VertexBuffer.get(), m_IndexBuffer.get(), cmd.IndexCount,
                                    cmd.IndexOffset, exec );
        }
    }
} // namespace Desert::Graphic::Render2D

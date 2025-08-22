#include "Grid.hpp"

namespace Desert::Editor::Render
{
    Common::BoolResult Grid::Init()
    {
        const auto& scene = m_DstScene.lock();
        if ( !scene )
        {
            return Common::MakeError( "Invalid scene" );
        }

        // Create grid mesh
        /*if ( !CreateGridGeometry() )
            return Common::MakeError( "Failed to create grid geometry" );*/

        // Setup pipeline
        /*if ( !SetupPipeline() )
            return Common::MakeError( "Failed to setup grid pipeline" );*/

        // Create grid material
       // m_Material = std::make_shared<MaterialGrid>();

        //m_Material->SetGridProperties( 1.0f, 20, glm::vec4( 0.5f, 0.5f, 0.5f, 0.3f ) );

        // Add to render graph
        constexpr std::string_view debugName = "EditorGrid";

        //scene->RegisterExternalPass(
        //     "EditorGridPass",
        //     [this]()
        //     {
        //         const auto& camera = m_DstScene.lock()->GetMainCamera();
        //         if ( camera )
        //         {
        //             // TODO: move to bind-class
        //             SetGridProperties( 16.025f, 0.025f, glm::vec4( 0.5f, 0.5f, 0.5f, 0.3f ) );
        //             UpdateCamera( camera.value() );

        //             auto& renderer = Graphic::Renderer::GetInstance();
        //             if ( const auto& material = m_Material )
        //             {
        //                 renderer.SubmitFullscreenQuad( m_Pipeline, material->GetMaterialExecutor() );
        //             }
        //         }
        //     },
        //     Graphic::RenderPass::Create(
        //          { .TargetFramebuffer = m_Framebuffer, .DebugName = "EditorGridPass" } ) );

        return BOOLSUCCESS;
    }

    bool Grid::CreateGridGeometry()
    {
        // m_MeshGrid = std::make_shared<Desert::Mesh>();
        return true;
    }

    bool Grid::SetupPipeline()
    {
        using namespace Graphic;

        const auto& scene = m_DstScene.lock();
        if ( !scene )
        {
            return false;
        }

        constexpr std::string_view debugName = "EditorGrid";

        m_Shader = Graphic::Shader::Create( "Grid.glsl" );

        // Framebuffer
        FramebufferSpecification fbSpec;
        fbSpec.DebugName               = debugName;

        fbSpec.ExternalAttachments.ColorAttachments.push_back(
             { .SourceFramebuffer = scene->GetCompositeFramebuffer(),
               .AttachmentIndex   = 0,
               .Load              = AttachmentLoad::Load } );

        fbSpec.ExternalAttachments.DepthAttachment = 
             { .SourceFramebuffer = scene->GetCompositeFramebuffer(),
               .AttachmentIndex   = 0,
               .Load              = AttachmentLoad::Load };

        m_Framebuffer = Graphic::Framebuffer::Create( fbSpec );

        m_Framebuffer->Resize( 1920, 780 );
        Graphic::PipelineSpecification pipeSpec;
        pipeSpec.DebugName         = debugName;
        pipeSpec.Framebuffer       = m_Framebuffer;
        pipeSpec.Shader            = m_Shader;
        pipeSpec.DepthTestEnabled  = true;
        pipeSpec.DepthWriteEnabled = true;
        pipeSpec.DepthCompareOp    = CompareOp::LessOrEqual;
        pipeSpec.CullMode          = CullMode::None;

        m_Pipeline = Graphic::Pipeline::Create( pipeSpec );
        m_Pipeline->Invalidate();

        return m_Pipeline != nullptr;
    }

    void Grid::SetGridProperties( float cellSize, float cellCount, const glm::vec4& color )
    {
        if ( m_Material )
        {
            m_Material->SetGridProperties( cellSize, cellCount, color );
        }
    }

    void Grid::UpdateCamera( const std::shared_ptr<Core::Camera>& camera )
    {
        if ( m_Material )
        {
            m_Material->UpdateRenderParameters( *camera );
        }
    }

} // namespace Desert::Editor::Render
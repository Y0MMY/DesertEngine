#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Core/Application.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Graphic/Materials/Models/PBR/PBRMaterialHelper.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
#define GEOMETRY_RENDERINFO( x ) m_SceneInfo.Renderdata.Geometry.InfoRender.x
#define SKYBOX_RENDERINFO( x ) m_SceneInfo.Renderdata.Skybox.InfoRender.x

    NO_DISCARD Common::BoolResult SceneRenderer::Init()
    {
        uint32_t width  = EngineContext::GetInstance().GetCurrentWindowWidth();
        uint32_t height = EngineContext::GetInstance().GetCurrentWindowHeight();

        // ==================== Skybox Pass ====================
        {
            auto&                      skybox    = m_SceneInfo.Renderdata.Skybox.InfoRender;
            constexpr std::string_view debugName = "Skybox";

            // Framebuffer
            FramebufferSpecification fbSpec;
            fbSpec.DebugName = debugName;
            fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA8F );

            skybox.Framebuffer = Graphic::Framebuffer::Create( fbSpec );
            skybox.Framebuffer->Resize( width, height );

            // RenderPass
            RenderPassSpecification rpSpec;
            rpSpec.DebugName         = debugName;
            rpSpec.TargetFramebuffer = skybox.Framebuffer;
            skybox.RenderPass        = Graphic::RenderPass::Create( rpSpec );

            // Pipeline
            skybox.Shader = Graphic::Shader::Create( "skybox.glsl" );

            Graphic::PipelineSpecification pipeSpec;
            pipeSpec.DebugName   = debugName;
            pipeSpec.Layout      = { { Graphic::ShaderDataType::Float3, "a_Position" } };
            pipeSpec.Framebuffer = skybox.Framebuffer;
            pipeSpec.Shader      = skybox.Shader;

            skybox.Pipeline = Graphic::Pipeline::Create( pipeSpec );
            skybox.Pipeline->Invalidate();

            // Material
            skybox.Material = Material::Create( std::string( debugName ), skybox.Shader );
            skybox.Material->Invalidate();

            skybox.UBManager = UniformBufferManager::Create( debugName, pipeSpec.Shader );
        }

        // ==================== Composite Pass ====================
        {
            auto&                      composite = m_SceneInfo.Renderdata.Composite;
            constexpr std::string_view debugName = "SceneComposite";

            // Framebuffer
            FramebufferSpecification fbSpec;
            fbSpec.DebugName = debugName;
            fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::BGRA8F );

            composite.Framebuffer = Graphic::Framebuffer::Create( fbSpec );
            composite.Framebuffer->Resize( width, height );

            // RenderPass
            RenderPassSpecification rpSpec;
            rpSpec.DebugName         = debugName;
            rpSpec.TargetFramebuffer = composite.Framebuffer;
            composite.RenderPass     = Graphic::RenderPass::Create( rpSpec );

            // Pipeline
            composite.Shader = Graphic::Shader::Create( "SceneComposite.glsl" );

            Graphic::PipelineSpecification pipeSpec;
            pipeSpec.DebugName   = debugName;
            pipeSpec.Layout      = { { Graphic::ShaderDataType::Float3, "a_Position" } };
            pipeSpec.Framebuffer = composite.Framebuffer;
            pipeSpec.Shader      = composite.Shader;

            composite.Pipeline = Graphic::Pipeline::Create( pipeSpec );
            composite.Pipeline->Invalidate();

            composite.Material = Material::Create( std::string( debugName ), composite.Shader );
            composite.Material->Invalidate();

            composite.UBManager = UniformBufferManager::Create( debugName, pipeSpec.Shader );
        }

        // ==================== Geometry Pass ====================
        {
            auto&                      geometry  = m_SceneInfo.Renderdata.Geometry.InfoRender;
            constexpr std::string_view debugName = "SceneGeometry";

            // Framebuffer
            FramebufferSpecification fbSpec;
            fbSpec.DebugName                = debugName;
            fbSpec.Attachments.Attachments  = { Core::Formats::ImageFormat::DEPTH24STENCIL8 };
            fbSpec.ExternalAttachments.Load = AttachmentLoad::Load;
            fbSpec.ExternalAttachments.ExternalAttachments.push_back( SKYBOX_RENDERINFO( Framebuffer ) );

            geometry.Framebuffer = Graphic::Framebuffer::Create( fbSpec );
            geometry.Framebuffer->Resize( width, height );

            // RenderPass
            RenderPassSpecification rpSpec;
            rpSpec.DebugName         = debugName;
            rpSpec.TargetFramebuffer = geometry.Framebuffer;
            geometry.RenderPass      = RenderPass::Create( rpSpec );

            // Pipeline
            PipelineSpecification pipeSpec;
            pipeSpec.DebugName   = debugName;
            pipeSpec.Layout      = { { Graphic::ShaderDataType::Float3, "a_Position" },
                                     { Graphic::ShaderDataType::Float3, "a_Normal" },
                                     { Graphic::ShaderDataType::Float3, "a_Tangent" },
                                     { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                                     { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };
            pipeSpec.Shader      = Graphic::Shader::Create( "StaticPBR.glsl" );
            pipeSpec.Framebuffer = geometry.Framebuffer;
            geometry.Shader      = pipeSpec.Shader;
            pipeSpec.Renderpass  = geometry.RenderPass;

            geometry.Pipeline = Pipeline::Create( pipeSpec );
            geometry.Pipeline->Invalidate();

            geometry.Material = Material::Create( std::string( debugName ), geometry.Shader );
            geometry.Material->Invalidate();

            geometry.UBManager = UniformBufferManager::Create( debugName, pipeSpec.Shader );
        }

        m_SceneInfo.EnvironmentData = EnvironmentManager::Create( "HDR/env.hdr" );

        {
            const auto ubLightRes = GEOMETRY_RENDERINFO( UBManager )->GetUniformBuffer( "LightningUB" );
            if ( !ubLightRes )
            {
                LOG_ERROR( "Lightning UB was not found!" );
                //  return Common::MakeError( "Lightning UB was not found!" );
            }
            m_SceneInfo.LightsInfo.Lightning = std::make_unique<Models::LightingData>(
                 glm::vec3( 0 ), ubLightRes.GetValue(), GEOMETRY_RENDERINFO( Material ) );
        }

        // PBR
        {
            const auto ubPBRRes = GEOMETRY_RENDERINFO( UBManager )->GetUniformBuffer( "PBRData" );
            if ( !ubPBRRes )
            {
                LOG_ERROR( "PBRData UB was not found!" );
                //  return Common::MakeError( "Lightning UB was not found!" );
            }
            m_SceneInfo.Renderdata.Geometry.PBRUB = std::make_unique<Models::PBR::PBRMaterial>(
                 GEOMETRY_RENDERINFO( Material ), ubPBRRes.GetValue() );

            m_SceneInfo.Renderdata.Geometry.PBRTextures =
                 std::make_unique<Models::PBR::PBRMaterialTexture>( GEOMETRY_RENDERINFO( Material ), nullptr );
        }

        {
            const auto ubGlobalRes = GEOMETRY_RENDERINFO( UBManager )->GetUniformBuffer( "GlobalUB" );
            if ( !ubGlobalRes )
            {
                LOG_ERROR( "GlobalUB UB was not found!" );

                // return Common::MakeError( "GlobalUB UB was not found!" );
            }

            m_SceneInfo.Renderdata.Geometry.GlobalUB =
                 std::make_unique<Models::GlobalData>( Models::GlobalUB{ .CameraPosition = glm::vec3( 0 ) },
                                                       ubGlobalRes.GetValue(), GEOMETRY_RENDERINFO( Material ) );
        }

        {
            const auto ubCameraRes = SKYBOX_RENDERINFO( UBManager )->GetUniformBuffer( "camera" );
            if ( !ubCameraRes )
            {
                LOG_ERROR( "camera UB was not found!" );

                // return Common::MakeError( "camera UB was not found!" );
            }

            m_SceneInfo.Renderdata.Skybox.CameraUB =
                 std::make_unique<Models::CameraData>( SKYBOX_RENDERINFO( Material ), ubCameraRes.GetValue() );
        }

        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResult SceneRenderer::BeginScene( const std::shared_ptr<Core::Scene>& scene,
                                                             const Core::Camera&                 camera )
    {
        m_SceneInfo.ActiveScene  = scene;
        m_SceneInfo.ActiveCamera = const_cast<Core::Camera*>( &camera );

        auto& renderer = Renderer::GetInstance();
        return renderer.BeginFrame();
    }

    void SceneRenderer::OnUpdate( DTO::SceneRendererUpdate&& sceneRenderInfo )
    {
        if ( sceneRenderInfo.DirLights.size() )
            m_SceneInfo.LightsInfo.Lightning->UpdateDirection(
                 std::move( sceneRenderInfo.DirLights[0].Direction ) );

        SkyboxRenderPass();
        GeometryRenderPass();
        ToneMapRenderPass();
        CompositeRenderPass();

        GEOMETRY_RENDERINFO( Material )->Clear();
        m_SceneInfo.Renderdata.Composite.Material->Clear();
        SKYBOX_RENDERINFO( Material )->Clear();
    }

    NO_DISCARD Common::BoolResult SceneRenderer::EndScene()
    {
        auto& renderer = Renderer::GetInstance();
        return renderer.EndFrame();
    }

    void SceneRenderer::Resize( const uint32_t width, const uint32_t height )
    {
        if ( width == 0 && height == 0 )
            return;
        auto& renderer = Renderer::GetInstance();
        renderer.ResizeWindowEvent( width, height );
        SKYBOX_RENDERINFO( Framebuffer )->Resize( width, height );
        m_SceneInfo.Renderdata.Composite.Framebuffer->Resize( width, height );
    }

    // NOTE: if you use rendering without imgui, you may get a black screen! you should start by setting
    // CompositePass!
    void SceneRenderer::CompositeRenderPass()
    {
        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        auto& renderer = Renderer::GetInstance();

        renderer.BeginSwapChainRenderPass();
        renderer.EndRenderPass();
    }

    void SceneRenderer::ToneMapRenderPass()
    {
        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();
        auto&    renderer   = Renderer::GetInstance();

        renderer.BeginRenderPass( m_SceneInfo.Renderdata.Composite.RenderPass );

        m_SceneInfo.Renderdata.Composite.Material->SetImage2D(
             "u_GeometryTexture", SKYBOX_RENDERINFO( Framebuffer )->GetColorAttachmentImage() );

        m_SceneInfo.Renderdata.Composite.Material->ApplyMaterial();
        renderer.SubmitFullscreenQuad( m_SceneInfo.Renderdata.Composite.Pipeline,
                                       m_SceneInfo.Renderdata.Composite.Material );
        renderer.EndRenderPass();
    }

    void SceneRenderer::SkyboxRenderPass()
    {
        auto& renderer = Renderer::GetInstance();

        renderer.BeginRenderPass( SKYBOX_RENDERINFO( RenderPass ) );

        m_SceneInfo.Renderdata.Skybox.CameraUB->UpdateCameraUB( *m_SceneInfo.ActiveCamera ); // TODO: constant push
        m_SceneInfo.Renderdata.Skybox.CameraUB->Bind();
        SKYBOX_RENDERINFO( Material )->ApplyMaterial();
        renderer.SubmitFullscreenQuad( SKYBOX_RENDERINFO( Pipeline ), SKYBOX_RENDERINFO( Material ) );

        renderer.EndRenderPass();
    }

    void SceneRenderer::GeometryRenderPass()
    {
        auto& renderer = Renderer::GetInstance();

        renderer.BeginRenderPass( GEOMETRY_RENDERINFO( RenderPass ) );

        for ( const auto& meshInfo : m_SceneInfo.Renderdata.MeshInfo )
        {
            struct VP
            {
                glm::mat4 Project;
                glm::mat4 View;
            };

            const VP vp{ .Project = m_SceneInfo.ActiveCamera->GetProjectionMatrix(),
                         .View    = m_SceneInfo.ActiveCamera->GetViewMatrix() };
            m_SceneInfo.Renderdata.Geometry.InfoRender.Material->PushConstant( &vp, sizeof( vp ) );

            Models::GlobalUB globlal = { .CameraPosition = m_SceneInfo.ActiveCamera->GetPosition() };

            m_SceneInfo.Renderdata.Geometry.GlobalUB->UpdateUBGlobal( std::move( globlal ) );
            m_SceneInfo.Renderdata.Geometry.PBRUB->UpdatePBR( {} );
            /*m_SceneInfo.Renderdata.Geometry.PBRTextures->UpdatePBR(
                 { .IrradianceMap  = m_SceneInfo.EnvironmentData.IrradianceMap,
                   .PreFilteredMap = m_SceneInfo.EnvironmentData.PreFilteredMap } );*/

            m_SceneInfo.LightsInfo.Lightning->Bind(); // NOTE: data has already been updated
            m_SceneInfo.Renderdata.Geometry.GlobalUB->Bind();
            m_SceneInfo.Renderdata.Geometry.PBRUB->Bind();
            //m_SceneInfo.Renderdata.Geometry.PBRTextures->Bind();

            GEOMETRY_RENDERINFO( Material )->ApplyMaterial();
            renderer.RenderMesh( GEOMETRY_RENDERINFO( Pipeline ), meshInfo.Mesh, GEOMETRY_RENDERINFO( Material ) );
        }

        renderer.EndRenderPass();
    }

    void SceneRenderer::RenderMesh( const std::shared_ptr<Mesh>& mesh )
    {
        m_SceneInfo.Renderdata.MeshInfo.push_back( { mesh } );
    }

    const Environment SceneRenderer::CreateEnvironment( const Common::Filepath& filepath )
    {
        return EnvironmentManager::Create( filepath );
    }

    void SceneRenderer::SetEnvironment( const Environment& environment )
    {
        m_SceneInfo.EnvironmentData = environment;

        SKYBOX_RENDERINFO( Material )->SetImageCube( "samplerCubeMap", m_SceneInfo.EnvironmentData.RadianceMap );
    }

    const Environment& SceneRenderer::GetEnvironment()
    {
        return m_SceneInfo.EnvironmentData;
    }

    void SceneRenderer::Shutdown()
    {
        if ( m_SceneInfo.EnvironmentData.IrradianceMap )
        {
            m_SceneInfo.EnvironmentData.IrradianceMap->Release();
            m_SceneInfo.EnvironmentData.IrradianceMap.reset();
        }

        if ( m_SceneInfo.EnvironmentData.PreFilteredMap )
        {
            m_SceneInfo.EnvironmentData.PreFilteredMap->Release();
            m_SceneInfo.EnvironmentData.PreFilteredMap.reset();
        }

        if ( m_SceneInfo.EnvironmentData.RadianceMap )
        {
            m_SceneInfo.EnvironmentData.RadianceMap->Release();
            m_SceneInfo.EnvironmentData.RadianceMap.reset();
        }

        m_SceneInfo.Renderdata.Composite.Framebuffer->Release();
        m_SceneInfo.Renderdata.Composite.Framebuffer.reset();

        GEOMETRY_RENDERINFO( Framebuffer )->Release();
        GEOMETRY_RENDERINFO( Framebuffer ).reset();

        SKYBOX_RENDERINFO( Framebuffer )->Release();
        SKYBOX_RENDERINFO( Framebuffer ).reset();
    }

    const std::shared_ptr<Desert::Graphic::Image2D> SceneRenderer::GetFinalImage() const
    {
        return m_SceneInfo.Renderdata.Composite.Framebuffer->GetColorAttachmentImage();
    }

} // namespace Desert::Graphic
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Core/Application.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Graphic/Materials/Models/PBR/PBRMaterialHelper.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
#define GEOMETRY_RENDERINFO( x ) m_SceneInfo.Renderdata.Geometry.InfoRender.x
#define SKYBOX_RENDERINFO( x ) m_SceneInfo.Renderdata.Skybox.InfoRender.x
#define COMPOSITE_RENDERINFO( x ) m_SceneInfo.Renderdata.Composite.InfoRender.x

    NO_DISCARD Common::BoolResult SceneRenderer::Init()
    {
        uint32_t width  = EngineContext::GetInstance().GetCurrentWindowWidth();
        uint32_t height = EngineContext::GetInstance().GetCurrentWindowHeight();

        if ( !InitSkyboxPass( width, height ) )
            return Common::MakeError( "Failed to initialize Skybox pass" );
        if ( !InitCompositePass( width, height ) )
            return Common::MakeError( "Failed to initialize Composite pass" );
        if ( !InitGeometryPass( width, height ) )
            return Common::MakeError( "Failed to initialize Geometry pass" );

        if ( !InitLightingUniforms() )
            return Common::MakeError( "Failed to initialize lighting uniforms" );
        if ( !InitSkyboxUniforms() )
            return Common::MakeError( "Failed to initialize skybox uniforms" );
        if ( !InitPBRUniforms() )
            return Common::MakeError( "Failed to initialize PBR uniforms" );
        if ( !InitGlobalUniforms() )
            return Common::MakeError( "Failed to initialize global uniforms" );
        if ( !InitCameraUniforms() )
            return Common::MakeError( "Failed to initialize camera uniforms" );
        if ( !InitToneMapUniforms() )
            return Common::MakeError( "Failed to initialize tone map uniforms" );

        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResult SceneRenderer::InitLightingUniforms()
    {

        m_SceneInfo.LightsInfo.Lightning =
             std::make_unique<Models::LightingData>( glm::vec3( 0 ), GEOMETRY_RENDERINFO( Material ) );

        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResult SceneRenderer::InitSkyboxUniforms()
    {
        m_SceneInfo.Renderdata.Skybox.SkyboxUB =
             std::make_unique<Models::SkyboxData>( SKYBOX_RENDERINFO( Material ) );

        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResult SceneRenderer::InitPBRUniforms()
    {
        m_SceneInfo.Renderdata.Geometry.PBRUB =
             std::make_unique<Models::PBR::PBRMaterial>( GEOMETRY_RENDERINFO( Material ) );

        m_SceneInfo.Renderdata.Geometry.PBRTextures =
             std::make_unique<Models::PBR::PBRMaterialTexture>( GEOMETRY_RENDERINFO( Material ) );

        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResult SceneRenderer::InitGlobalUniforms()
    {

        m_SceneInfo.Renderdata.Geometry.GlobalUB = std::make_unique<Models::GlobalData>(
             Models::GlobalUB{ .CameraPosition = glm::vec3( 0 ) }, GEOMETRY_RENDERINFO( Material ) );

        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResult SceneRenderer::InitCameraUniforms()
    {

        m_SceneInfo.Renderdata.Skybox.CameraUB =
             std::make_unique<Models::CameraData>( SKYBOX_RENDERINFO( Material ) );

        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResult SceneRenderer::InitToneMapUniforms()
    {

        m_SceneInfo.Renderdata.Composite.ToneMapUB =
             std::make_unique<Models::ToneMap>( COMPOSITE_RENDERINFO( Material ) );

        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResult SceneRenderer::InitSkyboxPass( const uint32_t width, const uint32_t height )
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

        const auto& texture = FallbackTextures::Get().GetFallbackTextureCube( Core::Formats::ImageFormat::RGBA8F );

        m_SceneInfo.EnvironmentData.RadianceMap    = texture;
        m_SceneInfo.EnvironmentData.IrradianceMap  = texture;
        m_SceneInfo.EnvironmentData.PreFilteredMap = texture;
        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResult SceneRenderer::InitCompositePass( const uint32_t width, const uint32_t height )
    {
        auto&                      composite = m_SceneInfo.Renderdata.Composite.InfoRender;
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

        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResult SceneRenderer::InitGeometryPass( const uint32_t width, const uint32_t height )
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

        m_SceneInfo.Renderdata.MeshInfo.clear();
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
        COMPOSITE_RENDERINFO( Framebuffer )->Resize( width, height );
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

        renderer.BeginRenderPass( COMPOSITE_RENDERINFO( RenderPass ) );

        m_SceneInfo.Renderdata.Composite.ToneMapUB->UpdateToneMap(
             SKYBOX_RENDERINFO( Framebuffer )->GetColorAttachmentImage() );

        renderer.SubmitFullscreenQuad( COMPOSITE_RENDERINFO( Pipeline ), COMPOSITE_RENDERINFO( Material ) );
        renderer.EndRenderPass();
    }

    void SceneRenderer::SkyboxRenderPass()
    {
        auto& renderer = Renderer::GetInstance();

        renderer.BeginRenderPass( SKYBOX_RENDERINFO( RenderPass ) );

        m_SceneInfo.Renderdata.Skybox.SkyboxUB->UpdateSkybox( m_SceneInfo.EnvironmentData.RadianceMap );
        m_SceneInfo.Renderdata.Skybox.CameraUB->UpdateCameraUB( *m_SceneInfo.ActiveCamera ); // TODO: constant push

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
                glm::mat4 ViewProject;
                glm::mat4 Transform;
            };

            const VP vp{ .ViewProject = m_SceneInfo.ActiveCamera->GetProjectionMatrix() *
                                        m_SceneInfo.ActiveCamera->GetViewMatrix(),
                         .Transform = meshInfo.Transform };
            m_SceneInfo.Renderdata.Geometry.InfoRender.Material->PushConstant( &vp, sizeof( vp ) );

            Models::GlobalUB globlal = { .CameraPosition = m_SceneInfo.ActiveCamera->GetPosition() };

            m_SceneInfo.Renderdata.Geometry.GlobalUB->UpdateUBGlobal( std::move( globlal ) );
            m_SceneInfo.Renderdata.Geometry.PBRUB->UpdatePBR( {} );
            m_SceneInfo.Renderdata.Geometry.PBRTextures->UpdatePBR(
                 { .IrradianceMap  = m_SceneInfo.EnvironmentData.IrradianceMap,
                   .PreFilteredMap = m_SceneInfo.EnvironmentData.PreFilteredMap } );

            renderer.RenderMesh( GEOMETRY_RENDERINFO( Pipeline ), meshInfo.Mesh, GEOMETRY_RENDERINFO( Material ) );
        }

        renderer.EndRenderPass();
    }

    void SceneRenderer::AddToRenderMeshList( const std::shared_ptr<Mesh>&     mesh,
                                             const std::shared_ptr<Material>& material,
                                             const glm::mat4&                 transform )
    {
        if ( mesh )
        {
            m_SceneInfo.Renderdata.MeshInfo.push_back( { mesh, transform } );
        }
    }

    const Environment SceneRenderer::CreateEnvironment( const Common::Filepath& filepath )
    {
        return EnvironmentManager::Create( filepath );
    }

    void SceneRenderer::SetEnvironment( const Environment& environment )
    {
        if ( environment )
        {
            m_SceneInfo.EnvironmentData = environment;
        }
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

        COMPOSITE_RENDERINFO( Framebuffer )->Release();
        COMPOSITE_RENDERINFO( Framebuffer ).reset();

        GEOMETRY_RENDERINFO( Framebuffer )->Release();
        GEOMETRY_RENDERINFO( Framebuffer ).reset();

        SKYBOX_RENDERINFO( Framebuffer )->Release();
        SKYBOX_RENDERINFO( Framebuffer ).reset();
    }

    const std::shared_ptr<Desert::Graphic::Image2D> SceneRenderer::GetFinalImage() const
    {
        return COMPOSITE_RENDERINFO( Framebuffer )->GetColorAttachmentImage();
    }

} // namespace Desert::Graphic
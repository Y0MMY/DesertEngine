#pragma once

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/Core/Camera.hpp>

#include <Common/Core/Events/WindowEvents.hpp>
#include <Common/Core/EventRegistry.hpp>

#include <Engine/Graphic/Materials/Models/Lighting.hpp>
#include <Engine/Graphic/Materials/Models/Global.hpp>
#include <Engine/Graphic/Materials/Models/PBR/PBRMaterialHelper.hpp>
#include <Engine/Graphic/Materials/Models/PBR/PBRTextures.hpp>
#include <Engine/Graphic/Materials/Models/Skybox/Skybox.hpp>
#include <Engine/Graphic/Materials/Models/Skybox/Camera.hpp>
#include <Engine/Graphic/Materials/Models/ToneMap.hpp>

namespace Desert::Core
{
    class Scene;
};

namespace Desert::Graphic
{
    class SceneRenderer final
    {
    public:
        [[nodiscard]] Common::BoolResult Init();
        void                             Shutdown();

        [[nodiscard]] Common::BoolResult BeginScene( const std::shared_ptr<Desert::Core::Scene>& scene,
                                                     const Core::Camera&                         camera );

        void OnUpdate( DTO::SceneRendererUpdate&& sceneRenderInfo );

        [[nodiscard]] Common::BoolResult EndScene();

        void Resize( const uint32_t width, const uint32_t height );

        void AddToRenderMeshList( const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Material>& material,
                                  const glm::mat4& transform );
        const Environment  CreateEnvironment( const Common::Filepath& filepath );
        void               SetEnvironment( const Environment& environment );
        const Environment& GetEnvironment();

        const std::shared_ptr<Image2D> GetFinalImage() const;

    private:
        [[nodiscard]] Common::BoolResult InitSkyboxPass( const uint32_t width, const uint32_t height );
        [[nodiscard]] Common::BoolResult InitCompositePass( const uint32_t width, const uint32_t height );
        [[nodiscard]] Common::BoolResult InitGeometryPass( const uint32_t width, const uint32_t height );

        [[nodiscard]] Common::BoolResult InitLightingUniforms();
        [[nodiscard]] Common::BoolResult InitSkyboxUniforms();
        [[nodiscard]] Common::BoolResult InitPBRUniforms();
        [[nodiscard]] Common::BoolResult InitGlobalUniforms();
        [[nodiscard]] Common::BoolResult InitCameraUniforms();
        [[nodiscard]] Common::BoolResult InitToneMapUniforms();

    private:
        void CompositeRenderPass();
        void ToneMapRenderPass();
        void GeometryRenderPass();
        void SkyboxRenderPass();

    private:
    private:
        struct MeshRenderInfo
        {
            std::shared_ptr<Mesh> Mesh;
            glm::mat4             Transform;
        };

        struct LightsRenderInfo
        {
            std::unique_ptr<Models::LightingData> Lightning;
        };

        struct RenderInfo
        {
            std::shared_ptr<Graphic::Shader>      Shader;
            std::shared_ptr<Graphic::Framebuffer> Framebuffer;
            std::shared_ptr<Graphic::RenderPass>  RenderPass;
            std::shared_ptr<Graphic::Pipeline>    Pipeline;
            std::shared_ptr<Graphic::Material>    Material;
        };

        struct
        {
            std::shared_ptr<Core::Scene> ActiveScene;
            Core::Camera*                ActiveCamera;
            Environment                  EnvironmentData;
            LightsRenderInfo             LightsInfo;

            struct
            {
                struct
                {
                    RenderInfo                          InfoRender;
                    std::unique_ptr<Models::CameraData> CameraUB;
                    std::unique_ptr<Models::SkyboxData> SkyboxUB;
                } Skybox;

                struct
                {
                    RenderInfo                       InfoRender;
                    std::unique_ptr<Models::ToneMap> ToneMapUB;
                } Composite;

                struct
                {
                    RenderInfo                                       InfoRender;
                    std::unique_ptr<Models::GlobalData>              GlobalUB;
                    std::unique_ptr<Models::PBR::PBRMaterial>        PBRUB;
                    std::unique_ptr<Models::PBR::PBRMaterialTexture> PBRTextures;
                } Geometry;

                std::vector<MeshRenderInfo> MeshInfo;
            } Renderdata;
        } m_SceneInfo;
    };
} // namespace Desert::Graphic
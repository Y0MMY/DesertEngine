#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/Materials/Mesh/MaterialSilhouette.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/SkinnedMaterialPBR.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>
#include <Engine/Graphic/RenderGraphBuilder.hpp>

#include <Engine/Geometry/SkinnedMesh.hpp>
#include <Engine/Geometry/StaticMesh.hpp>

namespace Desert::Graphic::System
{
    struct MeshRenderData
    {
        class Mesh* Mesh;
        glm::mat4   Transform;

        std::vector<MaterialInstance*> MaterialSlots;

        // optional
        std::vector<glm::mat4> BoneMatrices;

        bool Outlined = false;
    };

    class MeshRenderer final : public RenderSystem
    {
    public:
        struct StaticMeshRenderData
        {
            class Desert::StaticMesh*      Mesh = nullptr;
            glm::mat4                      Transform = glm::mat4( 1.0f );
            std::vector<MaterialInstance*> MaterialSlots;
            bool                           Outlined = false;
        };

        struct SkinnedMeshRenderData
        {
            class Desert::SkinnedMesh*         Mesh;
            glm::mat4                          Transform;
            class Graphic::SkinnedMaterialPBR* Material;
            std::vector<glm::mat4>             BoneMatrices;
            bool                               Outlined = false;
        };

        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override;
        virtual void                  Shutdown() override;
        virtual void                  RegisterPasses( RenderGraphBuilder& builder ) override;

        // Silhouette mask of the currently outlined meshes (white on the framebuffer clear color).
        // Consumed by JumpFloodOutlineRenderer to build the outline.
        std::shared_ptr<Framebuffer> GetSilhouetteMaskFramebuffer() const
        {
            return m_SilhouetteMaskFramebuffer;
        }

        void SubmitMesh( const MeshRenderData& data );
        void ClearQueues();

    private:
        bool SetupGeometryPass();
        bool SetupSkinnedGeometryPass();
        bool SetupSilhouettePass();

        void DrawStaticMeshes();
        void DrawSkinnedMeshes();
        void RegisterSilhouettePass( RenderGraphBuilder& builder );

        ShaderProtocols::PBRTexturesUB PreparePBRTextures() const;

        void UpdateGlobalUniforms( const Core::Camera* camera, const ShaderProtocols::PointLight& pointLights,
                                   const ShaderProtocols::DirectionLight& dirLights );

    private:
        // Static
        std::shared_ptr<GraphicsPipeline> m_StaticPipeline;
        std::shared_ptr<Shader>   m_GeometryShader;

        // Skinned
        std::shared_ptr<GraphicsPipeline> m_SkinnedPipeline;
        std::shared_ptr<Shader>   m_SkinnedShader;

        // Silhouette (mask for the Jump Flood outline)
        std::shared_ptr<GraphicsPipeline>   m_SilhouettePipeline;
        std::shared_ptr<Shader>             m_SilhouetteShader;
        std::unique_ptr<MaterialSilhouette> m_SilhouetteMaterial;
        std::shared_ptr<Framebuffer>        m_SilhouetteMaskFramebuffer;

        std::vector<StaticMeshRenderData>  m_StaticQueue;
        std::vector<SkinnedMeshRenderData> m_SkinnedQueue;

    private:
        // fallbacks
        std::unique_ptr<Graphic::StaticMaterialPBR>  m_StaticMaterialFallback;
        std::unique_ptr<Graphic::SkinnedMaterialPBR> m_SkinnedMaterialFallback;
    };
} // namespace Desert::Graphic::System

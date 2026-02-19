#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/Materials/Mesh/MaterialOutline.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/SkinnedMaterialPBR.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>
#include <Engine/Graphic/RenderGraphBuilder.hpp>

namespace Desert::Graphic::System
{
    struct StaticMeshRenderData
    {
        std::shared_ptr<Mesh>              Mesh;
        glm::mat4                          Transform;
        std::shared_ptr<StaticMaterialPBR> Material;
        bool                               Outlined = false;
    };

    struct SkinnedMeshRenderData
    {
        std::shared_ptr<Desert::SkinnedMesh>         Mesh;
        glm::mat4                                    Transform;
        std::shared_ptr<Graphic::SkinnedMaterialPBR> Material;
        std::vector<glm::mat4>                       BoneMatrices;
    };

    class MeshRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override;
        virtual void                  Shutdown() override;
        virtual void                  RegisterPasses( RenderGraphBuilder& builder ) override;

        void ToggleOutline( bool value )
        {
            m_OutlineDraw = value;
        }
        void DisableOutline()
        {
            m_OutlineDraw = false;
        }
        void SetOutlineColor( const glm::vec3& color )
        {
            m_OutlineColor = color;
        }
        void SetOutlineWidth( float width )
        {
            m_OutlineWidth = width;
        }

        void AddStaticMesh( const std::shared_ptr<Desert::Mesh>&      mesh,
                            const std::shared_ptr<StaticMaterialPBR>& material, const glm::mat4& transform );

        void AddSkinnedMesh( const std::shared_ptr<Desert::SkinnedMesh>& mesh,
                             const std::shared_ptr<SkinnedMaterialPBR>& material, const glm::mat4& transform,
                             const std::vector<glm::mat4>& boneMatrices );

        void ClearQueues();

    private:
        bool SetupGeometryPass();
        bool SetupSkinnedGeometryPass();
        bool SetupOutlinePass();

        void DrawStaticMeshes();
        void DrawSkinnedMeshes();
        void RegisterOutlinePass( RenderGraphBuilder& builder );

        ShaderProtocols::PBRTexturesUB PreparePBRTextures() const;

    private:
        // Static
        std::shared_ptr<Pipeline> m_StaticPipeline;
        std::shared_ptr<Shader>   m_GeometryShader;

        // Skinned
        std::shared_ptr<Pipeline> m_SkinnedPipeline;
        std::shared_ptr<Shader>   m_SkinnedShader;

        // Outline
        std::shared_ptr<Pipeline>        m_OutlinePipeline;
        std::shared_ptr<Shader>          m_OutlineShader;
        std::unique_ptr<MaterialOutline> m_OutlineMaterial;

        std::vector<StaticMeshRenderData>  m_StaticQueue;
        std::vector<SkinnedMeshRenderData> m_SkinnedQueue;

        bool      m_OutlineDraw  = true;
        glm::vec3 m_OutlineColor = glm::vec3( 1.0f, 0.5f, 0.0f );
        float     m_OutlineWidth = 0.005f;
    };
} // namespace Desert::Graphic::System

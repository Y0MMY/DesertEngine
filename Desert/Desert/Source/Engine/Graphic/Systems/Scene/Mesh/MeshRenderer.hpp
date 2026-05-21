#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/Materials/Mesh/MaterialOutline.hpp>
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
        };

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

        void SubmitMesh( const MeshRenderData& data );
        void ClearQueues();

    private:
        bool SetupGeometryPass();
        bool SetupSkinnedGeometryPass();
        bool SetupOutlinePass();

        void DrawStaticMeshes();
        void DrawSkinnedMeshes();
        void RegisterOutlinePass( RenderGraphBuilder& builder );

        ShaderProtocols::PBRTexturesUB PreparePBRTextures() const;

        void UpdateGlobalUniforms( const Core::Camera* camera, const ShaderProtocols::PointLight& pointLights,
                                   const ShaderProtocols::DirectionLight& dirLights );

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

    private:
        // fallbacks
        std::unique_ptr<Graphic::StaticMaterialPBR>  m_StaticMaterialFallback;
        std::unique_ptr<Graphic::SkinnedMaterialPBR> m_SkinnedMaterialFallback;
    };
} // namespace Desert::Graphic::System

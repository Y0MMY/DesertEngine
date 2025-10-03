#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/Models/MeshRenderData.hpp>
#include <Engine/Graphic/Materials/Mesh/MaterialOutline.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>
#include <Engine/Graphic/Models/DirectionLight.hpp>
#include <Engine/Graphic/RenderGraphBuilder.hpp>

namespace Desert::Graphic::System
{
    class MeshRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override;
        virtual void               Shutdown() override;
        virtual void               RegisterPasses( RenderGraphBuilder& builder ) override;

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

    private:
        bool                                    SetupGeometryPass();
        bool                                    SetupOutlinePass();
        std::optional<Models::PBR::PBRTextures> PreparePBRTextures() const;

    private:
        std::shared_ptr<Pipeline> m_GeometryPipeline;
        std::shared_ptr<Shader>   m_GeometryShader;

        // Outline
        std::shared_ptr<Pipeline>        m_OutlinePipeline;
        std::shared_ptr<Shader>          m_OutlineShader;
        std::unique_ptr<MaterialOutline> m_OutlineMaterial;

        bool      m_OutlineDraw  = true;
        glm::vec3 m_OutlineColor = glm::vec3( 1.0f, 0.5f, 0.0f );
        float     m_OutlineWidth = 0.005f;

    };
} // namespace Desert::Graphic::System
#pragma once

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Camera.hpp>

#include <Engine/Graphic/Models/MeshRenderData.hpp>
#include <Engine/Graphic/Materials/Mesh/MaterialOutline.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

#include <Engine/Graphic/RenderGraph.hpp>

namespace Desert::Graphic::System
{
    class MeshRenderer
    {
    public:
        Common::BoolResult Init( const uint32_t width, const uint32_t height,
                                 const std::shared_ptr<Framebuffer>& skyboxFramebufferExternal );
        void               Shutdown();

        void BeginScene( const Core::Camera& camera, const std::optional<Environment>& environment );
        void Submit( const MeshRenderData& renderData );
        void SubmitLight( const glm::vec3& directionLight );
        void EndScene();

        std::shared_ptr<Framebuffer> GetFramebuffer() const
        {
            return m_Framebuffer;
        }

        void ToggleOutline( const bool value )
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

        void SetOutlineWidth( const float width )
        {
            // m_OutlineWidth = width;
        }

    private:
        bool                                    SetupGeometryPass( const uint32_t width, const uint32_t height,
                                                                   const std::shared_ptr<Framebuffer>& skyboxFramebufferExternal );
        bool                                    SetupOutlinePass( const uint32_t width, const uint32_t height,
                                                                  const std::shared_ptr<Framebuffer>& skyboxFramebufferExternal );
        std::optional<Models::PBR::PBRTextures> PreparePBRTextures() const;

    private:
        std::optional<Environment> m_Environment;

    private:
        std::vector<MeshRenderData> m_RenderQueue;
        glm::vec3                   m_DirectionLight;
        Core::Camera*               m_ActiveCamera = nullptr;

        std::shared_ptr<RenderPass> m_RenderPass;

        std::shared_ptr<Framebuffer> m_Framebuffer;
        std::shared_ptr<Pipeline>    m_Pipeline;
        std::shared_ptr<Shader>      m_Shader;

        // Outline
        std::shared_ptr<Shader>   m_OutlineShader;
        std::shared_ptr<Pipeline> m_OutlinePipeline;

        std::unique_ptr<MaterialOutline> m_OutlineMaterial;

        bool      m_OutlineDraw = true;
        glm::vec3 m_OutlineColor;
        float     m_OutlineWidth = 0.005;
    };
} // namespace Desert::Graphic::System
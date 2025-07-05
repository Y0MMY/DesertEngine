#pragma once

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Camera.hpp>

#include <Engine/Graphic/DTO/MeshRenderData.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

namespace Desert::Graphic::System
{
    class MeshRenderer
    {
    public:
        Common::BoolResult Init( const uint32_t width, const uint32_t height,
                                 const std::shared_ptr<Framebuffer>& skyboxFramebufferExternal );
        void               Shutdown();

        void BeginScene( const Core::Camera& camera, const std::optional<Environment>& environment );
        void Submit( const DTO::MeshRenderData& renderData );
        void EndScene();

        std::shared_ptr<Framebuffer> GetFramebuffer() const
        {
            return m_Framebuffer;
        }

    private:
        bool                                    SetupGeometryPass( const uint32_t width, const uint32_t height,
                                                                   const std::shared_ptr<Framebuffer>& skyboxFramebufferExternal );
        std::optional<Models::PBR::PBRTextures> PreparePBRTextures() const;

    private:
        std::optional<Environment> m_Environment;

    private:
        std::vector<DTO::MeshRenderData> m_RenderQueue;
        Core::Camera*                    m_ActiveCamera = nullptr;

        std::shared_ptr<Framebuffer> m_Framebuffer;
        std::shared_ptr<RenderPass>  m_RenderPass;
        std::shared_ptr<Pipeline>    m_Pipeline;
        std::shared_ptr<Shader>      m_Shader;
    };
} // namespace Desert::Graphic::System
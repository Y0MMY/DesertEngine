#pragma once

#include <Engine/Graphic/Environment/SceneEnvironment.hpp>
#include <Engine/Graphic/Materials/MaterialSkybox.hpp>

namespace Desert::Graphic::System
{
    class SkyboxRenderer
    {
    public:
        [[nodiscard]] Common::BoolResult Init( const uint32_t width, const uint32_t height );
        void                             Shutdown()
        {
        }

        void BeginScene( const Core::Camera& camera );
        void Submit( const std::shared_ptr<MaterialSkybox>& material )
        {
            m_MaterialSkybox = material;
        }
        void EndScene();

        const auto& GetFramebuffer() const
        {
            return m_Framebuffer;
        }

    private:
        std::weak_ptr<MaterialSkybox> m_MaterialSkybox;

        Core::Camera* m_ActiveCamera = nullptr;

        std::shared_ptr<Framebuffer> m_Framebuffer;
        std::shared_ptr<RenderPass>  m_RenderPass;
        std::shared_ptr<Pipeline>    m_Pipeline;
        std::shared_ptr<Shader>      m_Shader;
    };
} // namespace Desert::Graphic::System
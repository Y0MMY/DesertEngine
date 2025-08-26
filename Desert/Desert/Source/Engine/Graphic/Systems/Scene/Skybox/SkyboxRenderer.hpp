#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Environment/SceneEnvironment.hpp>
#include <Engine/Graphic/Materials/Skybox/MaterialSkybox.hpp>

namespace Desert::Graphic::System
{
    class SkyboxRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResult Initialize() override;
        void                       Shutdown()
        {
        }

        void PrepareCamera( const std::shared_ptr<Core::Camera>& camera );
        void PrepareMaterial( const std::shared_ptr<MaterialSkybox>& material );

        const std::optional<Environment> GetEnvironment() const
        {
            if ( const auto& material = m_MaterialSkybox.lock() )
            {
                return material->GetEnvironment();
            }
            return std::nullopt;
        }

    private:
        std::weak_ptr<MaterialSkybox> m_MaterialSkybox;

        std::weak_ptr<Core::Camera> m_ActiveCamera;
        std::shared_ptr<Pipeline>   m_Pipeline;
        std::shared_ptr<Shader>     m_Shader;
    };
} // namespace Desert::Graphic::System
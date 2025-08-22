#pragma once

#include <Engine/Desert.hpp>

#include "../IRender.hpp"
#include "MaterialGrid.hpp"

namespace Desert::Editor::Render
{
    class Grid final : public IRender
    {
    public:
        using IRender::IRender;

        virtual Common::BoolResult Init() override;
        void                       UpdateCamera( const std::shared_ptr<Core::Camera>& camera );
        void                       SetGridProperties( float cellSize, float cellCount, const glm::vec4& color );

        virtual void Render() override
        {
        }

    private:
        bool CreateGridGeometry();
        bool SetupPipeline();

        std::shared_ptr<Graphic::Pipeline>    m_Pipeline;
        std::shared_ptr<Graphic::Shader>      m_Shader;
        std::shared_ptr<Graphic::Framebuffer> m_Framebuffer;
        std::shared_ptr<MaterialGrid>         m_Material;
    };

} // namespace Desert::Editor::Render
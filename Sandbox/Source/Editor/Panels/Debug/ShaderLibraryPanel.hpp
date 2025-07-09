#pragma once
#include "Editor/Panels/IPanel.hpp"
#include <Engine/Desert.hpp>

namespace Desert::Editor
{
    class ShaderLibraryPanel : public IPanel
    {
    public:
        ShaderLibraryPanel();
        void OnUIRender() override;
        void RenderShaderCodeWindow();

    private:
        Graphic::Shader* m_SelectedShader;
        std::string      m_ShaderSource;
        bool             m_ShowShaderCode = false;
    };
} // namespace Desert::Editor
#pragma once

#include "../IPanel.hpp"

#include <string>

namespace Desert::Editor
{
    // VISUAL STUB (no real functionality yet): the future "package the game" dialog — target platform,
    // configuration, output folder, scene list. The Build button is disabled until a real packaging
    // pipeline exists. Hidden by default; enable via View -> Build Settings.
    class BuildSettingsPanel final : public IPanel
    {
    public:
        BuildSettingsPanel() : IPanel( "Build Settings", /*showPanel=*/false )
        {
        }
        void OnUIRender() override;

    private:
        int         m_Platform  = 0;
        int         m_Config    = 1;
        std::string m_OutputDir = "Build/Output";
    };
} // namespace Desert::Editor

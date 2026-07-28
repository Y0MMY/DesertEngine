#pragma once

#include "../IPanel.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace Desert::Editor
{
    // The "package the game" dialog. Build BAKES the open project into a self-contained game folder
    // (Runtime binary + Assets + Cooked + engine shaders + run.sh) via GamePackager, on a JobSystem
    // worker so the UI never stalls. Platform selection beyond the host is still a placeholder.
    // Hidden by default; enable via View -> Build Settings.
    class BuildSettingsPanel final : public IPanel
    {
    public:
        BuildSettingsPanel() : IPanel( "Build Settings", /*showPanel=*/false )
        {
        }

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 560.0f, 640.0f );
        }

        void OnUIRender() override;

    private:
        void RescanScenes(); // fills m_Scenes with project-relative .desce paths

        int         m_Platform  = 0;
        int         m_Config    = 1;
        bool        m_AppBundle = true; // macOS: .app with bundled MoltenVK/loader
        std::string m_OutputDir = "Build/Output";

        // Async packaging state (worker writes, UI reads).
        std::atomic<bool> m_Building{ false };
        std::atomic<bool> m_HasResult{ false };
        bool              m_LastSuccess = false;
        std::string       m_LastMessage;    // guarded by the m_Building/m_HasResult handshake
        std::string       m_LastPackageDir;

        // Startup-scene picker: the .desce scenes found under the project (relative to the project
        // dir), scanned lazily on first render and via the Rescan button.
        std::vector<std::string> m_Scenes;
        bool                     m_ScenesScanned = false;
    };
} // namespace Desert::Editor

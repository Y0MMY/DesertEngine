#pragma once

#include "../IPanel.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace Desert::Editor
{
    // The "package the game" dialog. Build BAKES the open project into a self-contained game folder
    // (Runtime binary + Assets + Cooked + engine shaders + launcher) via GamePackager, on a JobSystem
    // worker so the UI never stalls. Hidden by default; enable via View -> Build Settings.
    //
    // THE PANEL HOLDS NO SETTINGS OF ITS OWN. The three packaging answers live in EditorPreferences
    // (editor.json) and are edited there in place; the target platform is not an answer at all, because
    // this editor packages for its own host and for nothing else — see Editor/Packaging/PackageTarget.hpp
    // for why, and the panel says so on screen rather than offering a choice it cannot honour.
    //
    // Everything declared below is bookkeeping for the async job and the startup-scene combo, and
    // Desert/Tests/Editor/BuildSettingsConsumers is the census that holds that line: a member this panel
    // puts inside an editing widget has to name the code that reads it.
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

        // Async packaging state (worker writes, UI reads).
        std::atomic<bool> m_Building{ false };
        std::atomic<bool> m_HasResult{ false };
        bool              m_LastSuccess = false;
        std::string       m_LastMessage; // guarded by the m_Building/m_HasResult handshake
        std::string       m_LastPackageDir;

        // Startup-scene picker: the .desce scenes found under the project (relative to the project
        // dir), scanned lazily on first render and via the Rescan button.
        std::vector<std::string> m_Scenes;
        bool                     m_ScenesScanned = false;
    };
} // namespace Desert::Editor

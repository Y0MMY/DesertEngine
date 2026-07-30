#pragma once

#include "../IPanel.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace Desert::Core
{
    class Scene;
}
namespace Desert::Assets
{
    class AssetManager;
}

namespace Desert::Editor
{
    // "Model from Photos": a TOOL-AGNOSTIC photogrammetry wrapper. The engine can't reconstruct 3D itself — it
    // runs a configurable EXTERNAL command (Meshroom / COLMAP / any CLI) on a photos folder, then imports the
    // produced mesh through the normal cook pipeline and spawns it into the scene. The command lives in
    // EditorPreferences ({input}/{output}/{outdir} placeholders). The tool runs on a worker thread; the import
    // + spawn happen on the main thread once it finishes. Hidden by default; View -> Model from Photos.
    class PhotogrammetryPanel final : public IPanel
    {
    public:
        PhotogrammetryPanel( const std::shared_ptr<::Desert::Core::Scene>& scene, Assets::AssetManager* assets );
        ~PhotogrammetryPanel() override;

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 640.0f, 420.0f );
        }
        void OnUIRender() override;

    private:
        enum class Job
        {
            None,
            Capture,     // grab frames from the camera into the photos folder
            Reconstruct, // run photogrammetry on the photos -> mesh
        };

        void RunCommand( const std::string& cmd, Job job ); // launches the worker thread
        void StartCapture();
        void StartReconstruction();
        void ImportResult(); // main-thread: cook the produced mesh + spawn an entity

        std::shared_ptr<::Desert::Core::Scene> m_Scene;
        Assets::AssetManager*                  m_Assets = nullptr;

        std::thread       m_Worker;
        std::atomic<bool> m_Running{ false };
        std::atomic<bool> m_Done{ false };
        std::atomic<int>  m_ExitCode{ 0 };
        Job               m_Job = Job::None;
        std::string       m_OutputCaptured; // resolved output mesh path for a running Reconstruct
        std::string       m_Status;
        bool              m_StatusError = false;
    };
} // namespace Desert::Editor

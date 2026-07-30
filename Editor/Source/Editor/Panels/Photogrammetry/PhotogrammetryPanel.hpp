#pragma once

#include "../IPanel.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
    namespace UI
    {
        class UIHelper;
    }
    class ThumbnailCache;
} // namespace Desert::Editor

namespace Desert::Editor
{
    // "Model from Photos": a TOOL-AGNOSTIC capture-to-mesh pipeline laid out like a DCC tool (MetaHuman
    // Animator style). The engine can't reconstruct 3D itself — it drives a configurable EXTERNAL CLI
    // (Meshroom / COLMAP / RealityCapture / your face solver) over a photos folder, streams its output
    // live, then imports the produced mesh through the normal cook pipeline and spawns it into the scene.
    //
    // Layout: a stage toolbar (Capture -> Reconstruct -> Import, plus Cancel / Open / Reimport), a source
    // pane with a thumbnail grid of the captured frames, a settings pane (tool presets + editable commands
    // + output picker), and a live log pane. Two modes swap presets + guidance: Object vs Face. Commands
    // live in EditorPreferences ({input}/{output}/{outdir}/{photos} placeholders). The tool runs on a worker
    // thread; the import + spawn happen on the main thread once it finishes. Hidden by default; View -> Model
    // from Photos.
    class PhotogrammetryPanel final : public IPanel
    {
    public:
        PhotogrammetryPanel( const std::shared_ptr<::Desert::Core::Scene>& scene, Assets::AssetManager* assets );
        ~PhotogrammetryPanel() override;

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 940.0f, 640.0f );
        }
        void OnUIRender() override;

    private:
        enum class Job
        {
            None,
            Capture,     // grab frames from the camera into the photos folder
            Reconstruct, // run the reconstruction tool on the photos -> mesh
        };
        enum class Mode
        {
            Object = 0, // generic object photogrammetry
            Face   = 1, // MetaHuman-style face capture (presets + guidance only; solver is external)
        };

        // --- pipeline ---
        void RunCommand( const std::string& cmd, Job job ); // launches the worker thread (streams stdout)
        void StartCapture();
        void StartReconstruction();
        void CancelJob();    // SIGTERM the running external process group
        void ImportResult(); // main-thread: cook the produced mesh + spawn an entity
        void Reimport();     // re-run the import on the last output without re-reconstructing
        void OpenOutputFolder();

        // --- ui sections ---
        void DrawToolbar( bool running );
        void DrawSourcePane();
        void DrawSettingsPane();
        void DrawLogPane( bool running );

        // --- helpers ---
        void RescanPhotos(); // refresh m_PhotoFiles from the photos folder
        void PushLog( const std::string& line );

        std::shared_ptr<::Desert::Core::Scene> m_Scene;
        Assets::AssetManager*                  m_Assets = nullptr;

        std::unique_ptr<UI::UIHelper>   m_UIHelper;
        std::unique_ptr<ThumbnailCache> m_Thumbnails;

        std::thread       m_Worker;
        std::atomic<bool> m_Running{ false };
        std::atomic<bool> m_Done{ false };
        std::atomic<int>  m_ExitCode{ 0 };
        std::atomic<long> m_ChildPid{ -1 }; // pid of the running external process (POSIX); -1 when none
        Job               m_Job  = Job::None;
        Mode              m_Mode = Mode::Object;
        std::string       m_OutputCaptured; // resolved output mesh path for a running/last Reconstruct
        std::string       m_Status;
        bool              m_StatusError = false;

        std::mutex               m_LogMutex;
        std::vector<std::string> m_Log; // external-process output, newest at the back
        bool                     m_LogAutoScroll = true;

        std::vector<std::string> m_PhotoFiles; // absolute paths of images in the photos folder
        std::string              m_ScannedDir; // folder m_PhotoFiles was built from
        int                      m_SelectedPhoto = -1;
        bool                     m_PhotosDirty   = true; // force a rescan next frame

        float m_SplitRatio = 0.60f; // source-pane / settings-pane split
    };
} // namespace Desert::Editor

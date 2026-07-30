#pragma once

#include "../IPanel.hpp"

#include <Engine/Assets/Common.hpp>
#include <Engine/ECS/Entity.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Desert::Core
{
    class Scene;
}
namespace Desert::Graphic
{
    class SceneRenderer;
    class Image2D;
} // namespace Desert::Graphic
namespace Desert::Assets
{
    class AssetManager;
}
namespace Common::Utils
{
    class CameraCapture;
}
namespace Desert::Editor
{
    namespace UI
    {
        class UIHelper;
    }
} // namespace Desert::Editor

namespace Desert::Editor
{
    // "Model from Camera": a live capture-to-mesh tool laid out like MetaHuman Animator — a split view with
    // the reconstructed 3D model (left) and the live webcam feed + landmark overlay (right), driven only by
    // the camera. Frames are captured from the webcam (AVFoundation) into a folder, an EXTERNAL reconstruction
    // tool (Meshroom / COLMAP / ...) turns them into a mesh (streamed live into the log), and the result is
    // imported + spawned + shown spinning in the left viewport. Hidden by default; View -> Model from Photos.
    class PhotogrammetryPanel final : public IPanel
    {
    public:
        PhotogrammetryPanel( const std::shared_ptr<::Desert::Core::Scene>& scene, Assets::AssetManager* assets );
        ~PhotogrammetryPanel() override;

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 1040.0f, 660.0f );
        }
        void OnUIRender() override;

    private:
        enum class Job
        {
            None,
            Reconstruct,
        };

        // --- camera ---
        void StartCamera();
        void StopCamera();
        void UpdateCamera(); // poll the newest frame -> GPU texture (throttled) + save it when recording

        // --- reconstruction pipeline ---
        void RunCommand( const std::string& cmd, Job job ); // worker thread, streams stdout
        void StartReconstruction();
        void CancelJob();
        void ImportResult();
        void Reimport();
        void LoadMeshFile(); // import + preview an arbitrary mesh (test the viewport without a tool)
        void OpenOutputFolder();

        // --- live mesh preview (own offscreen scene, mirrors AssetThumbnailRenderer) ---
        void EnsurePreview();
        void RenderPreview( uint32_t w, uint32_t h );
        void SetPreviewMesh( const Assets::AssetHandle& mesh );

        // --- ui sections ---
        void DrawToolbar( bool running );
        void DrawPreviewPane( const ImVec2& size ); // left: reconstructed model
        void DrawCameraPane( const ImVec2& size );  // right: live camera + landmarks
        void DrawBottom( bool running );            // settings + log + status

        // --- helpers ---
        void PushLog( const std::string& line );

        std::shared_ptr<::Desert::Core::Scene> m_Scene;
        Assets::AssetManager*                  m_Assets = nullptr;

        std::unique_ptr<UI::UIHelper> m_UIHelper;

        // Camera.
        std::unique_ptr<Common::Utils::CameraCapture> m_Camera;
        std::shared_ptr<Graphic::Image2D>             m_CameraImage; // rebuilt from the newest frame (throttled)
        std::vector<uint8_t>                          m_FrameBuf;    // scratch for the latest RGBA frame
        int                                           m_CamW           = 0;
        int                                           m_CamH           = 0;
        bool                                          m_CameraOn       = false;
        bool                                          m_Recording      = false;
        int                                           m_RecordedFrames = 0;
        double                                        m_LastTexTime    = 0.0; // throttle clock (ImGui time)
        double                                        m_LastSaveTime   = 0.0;

        // Reconstruction worker.
        std::thread       m_Worker;
        std::atomic<bool> m_Running{ false };
        std::atomic<bool> m_Done{ false };
        std::atomic<int>  m_ExitCode{ 0 };
        std::atomic<long> m_ChildPid{ -1 };
        Job               m_Job = Job::None;
        std::string       m_OutputCaptured;
        std::string       m_Status;
        bool              m_StatusError = false;

        std::mutex               m_LogMutex;
        std::vector<std::string> m_Log;
        bool                     m_LogAutoScroll = true;

        // Live mesh preview scene.
        std::unique_ptr<Graphic::SceneRenderer> m_PreviewRenderer;
        std::shared_ptr<::Desert::Core::Scene>  m_PreviewScene;
        ECS::Entity                             m_PreviewTarget;
        Assets::AssetHandle                     m_PreviewMesh{ static_cast<uint64_t>( 0 ) };
        bool                                    m_PreviewInit = false;
        uint32_t                                m_PreviewW    = 0;
        uint32_t                                m_PreviewH    = 0;
        float                                   m_Spin        = 0.0f;
    };
} // namespace Desert::Editor

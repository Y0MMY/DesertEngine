#pragma once

#include <Engine/Desert.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace Desert::Graphic
{
    class GraphicsPipeline;
    class MaterialExecutor;
} // namespace Desert::Graphic
namespace Desert::Graphic::Render2D
{
    class Render2D;
}

namespace Desert::Player
{
    // The PLAYER layer: loads the opened project's scene, flips it straight into Play (scripts, physics,
    // gameplay camera all live) and presents the rendered frame fullscreen through a chrome-less ImGui
    // window. No panels, no gizmos, no editing — the game, exactly as Play-in-editor runs it.
    // (namespace Player: Desert::Runtime already belongs to the engine's runtime services.)
    class RuntimeLayer : public Common::Layer
    {
    public:
        // scenePathOverride: from `--scene <path>`; empty -> the project's DefaultScene.
        explicit RuntimeLayer( std::string scenePathOverride );
        ~RuntimeLayer();

        [[nodiscard]] Common::BoolResultStr OnAttach() override;
        [[nodiscard]] Common::BoolResultStr OnDetach() override;
        [[nodiscard]] Common::BoolResultStr OnUpdate( const Common::Timestep& ts ) override;
        [[nodiscard]] Common::BoolResultStr OnImGuiRender() override;
        void                                OnEvent( Common::Event& event ) override;

    private:
        // Tear down the current scene and deserialize `path` in its place (systems survive Clear()). Runs
        // between frames from OnUpdate — a UI button's "scene:<path>" click queues it into m_PendingSceneLoad.
        void LoadSceneInternal( const std::string& path );

    private:
        std::string m_ScenePathOverride;

        std::shared_ptr<Assets::AssetManager>        m_AssetManager;
        std::unique_ptr<Assets::AssetPreloader>      m_AssetPreloader;
        std::unique_ptr<Animation::AnimationLibrary> m_AnimationLibrary;
        std::unique_ptr<Graphic::SceneRenderer>      m_SceneRenderer;
        std::shared_ptr<Core::Scene>                 m_Scene;

        // No-ImGui present: the runtime opens the swapchain pass itself, blits the scene's final image with a
        // fullscreen quad, then draws the UI + splash with the engine's own Render2D batcher. Lazily created on
        // the first present (the swapchain framebuffer only exists after the first BeginSwapChainRenderPass).
        std::unique_ptr<Graphic::Render2D::Render2D> m_Render2D;
        std::shared_ptr<Graphic::GraphicsPipeline>   m_BlitPipeline;
        std::unique_ptr<Graphic::MaterialExecutor>   m_BlitExecutor;
        bool                                         m_PresentReady  = false;
        bool                                         m_PrevMouseDown = false; // for the click (down->up) edge
        Common::BoolResultStr InitPresent( const std::shared_ptr<Graphic::Framebuffer>& swapFb );

        // Scene::Resize destroys GPU resources — deferred to the top of OnUpdate (same rule as the
        // editor's viewport panel).
        std::optional<std::pair<uint32_t, uint32_t>> m_PendingResize;
        uint32_t                                     m_LastWidth = 0, m_LastHeight = 0;

        // A UI button clicked this frame with an "scene:<path>" OnClickMessage — applied next OnUpdate.
        std::optional<std::string> m_PendingSceneLoad;

        // Splash screen (SceneSettings.Splash*): a full-screen image shown when a scene loads, fading in/out.
        // Armed by TriggerSplash() on load; m_SplashTimer counts down each frame.
        Assets::AssetHandle m_SplashSprite;
        float               m_SplashTimer    = 0.0f;
        float               m_SplashDuration = 0.0f;
        float               m_SplashFade     = 0.4f;
        void                TriggerSplash();
    };
} // namespace Desert::Player

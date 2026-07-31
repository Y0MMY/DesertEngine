#pragma once

#include <Engine/Desert.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>

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

        std::shared_ptr<::Desert::ImGui::ImGuiLayer> m_ImGuiLayer;
        std::unique_ptr<Graphic::UICacheTexture>     m_UITextureCache;

        // Scene::Resize destroys GPU resources — deferred to the top of OnUpdate (same rule as the
        // editor's viewport panel).
        std::optional<std::pair<uint32_t, uint32_t>> m_PendingResize;
        uint32_t                                     m_LastWidth = 0, m_LastHeight = 0;

        // A UI button clicked this frame with an "scene:<path>" OnClickMessage — applied next OnUpdate.
        std::optional<std::string> m_PendingSceneLoad;
    };
} // namespace Desert::Player

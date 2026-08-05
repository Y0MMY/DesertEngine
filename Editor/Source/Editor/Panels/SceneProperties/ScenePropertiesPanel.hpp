#pragma once

#include <Engine/Desert.hpp>

#include "../IPanel.hpp"

#include <Editor/Widgets/PreviewViewport.hpp>
#include <Editor/Widgets/UIHelper/ImGuiUI.hpp>

#include <Common/Core/Constants.hpp>

namespace Desert::Editor
{
    class ScenePropertiesPanel final : public IPanel
    {
    public:
        explicit ScenePropertiesPanel( const std::shared_ptr<Desert::Core::Scene>&  scene,
                                       const std::shared_ptr<Assets::AssetManager>& assetManager,
                                       const Animation::AnimationLibrary*           animationLibrary )
             : IPanel( "Details" ), m_Scene( scene ), m_AssetManager( assetManager ),
               m_AnimationLibrary( animationLibrary )
        {
        }
        void OnUIRender() override;
        // Where the asset preview is rendered — offscreen work must never happen inside the ImGui pass.
        void OnPreUpdate() override;
        void SetScene( const std::shared_ptr<Desert::Core::Scene>& scene ) override
        {
            m_Scene = scene;
        }

    private:
        void DrawMaterialEntity( const ECS::Entity& entity );

        // The live asset preview above the component list. ONE renderer for the whole panel (a
        // SceneRenderer is not cheap), reused as the selection changes.
        void DrawPreviewSection();

    private:
        std::shared_ptr<Desert::Core::Scene>        m_Scene;
        const std::shared_ptr<Assets::AssetManager> m_AssetManager;
        const Animation::AnimationLibrary*          m_AnimationLibrary;
        bool                                        m_DebugMode = false;
        std::string m_PrefabSavePath = Common::Constants::Path::PREFAB_PATH.string(); // post-remap

        // --- Asset preview -------------------------------------------------------------------------
        PreviewViewport               m_Preview;
        std::unique_ptr<UI::UIHelper> m_PreviewUI;       // texture-id cache for the preview image (lazy)
        uint64_t                      m_PreviewKey  = 0; // what the preview currently shows; a change re-frames it
        bool                          m_PreviewOpen = true;
        bool                          m_PreviewActive = false; // measured last UI frame: expanded + panel visible
        uint32_t                      m_PreviewWidth  = 0;     // size the preview occupied last UI frame
        uint32_t                      m_PreviewHeight = 0;
        float                         m_PreviewAspect = 16.0f / 10.0f;
    };
} // namespace Desert::Editor
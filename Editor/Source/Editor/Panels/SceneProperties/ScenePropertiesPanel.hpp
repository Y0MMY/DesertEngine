#pragma once

#include <Engine/Desert.hpp>

#include "../IPanel.hpp"

#include "ComponentEditor.hpp"

#include <Editor/Widgets/PreviewViewport.hpp>
#include <Editor/Widgets/ThumbnailCache.hpp>
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

        // UE-style property search, drawn above the scrolling component list.
        void DrawSearchBox();

    private:
        std::shared_ptr<Desert::Core::Scene>        m_Scene;
        const std::shared_ptr<Assets::AssetManager> m_AssetManager;
        const Animation::AnimationLibrary*          m_AnimationLibrary;
        bool                                        m_DebugMode = false;
        std::string m_PrefabSavePath = Common::Constants::Path::PREFAB_PATH.string(); // post-remap

        // Details search text; empty = show everything. Owned by the panel (one search per Details view).
        std::string m_FieldSearch;
        // The component list renderer. A member, not a function-static: several Details panels can exist
        // (one per scene view) and they must not share one editor's state.
        std::unique_ptr<ComponentEditor> m_ComponentEditor;

        // --- Asset preview -------------------------------------------------------------------------
        // ONE renderer for the whole panel (a SceneRenderer is not cheap), lent to whichever component
        // wants a thumbnail of what the entity renders — today the 3D Model row. The panel keeps the
        // frame ordering (Update in OnPreUpdate); a component only blits the last image.
        PreviewViewport               m_Preview;
        std::unique_ptr<UI::UIHelper> m_PreviewUI;       // texture-id cache for the preview image (lazy)
        uint64_t                      m_PreviewKey  = 0; // what the preview currently shows; a change re-frames it
        bool                          m_PreviewActive = false; // a component drew it during the last UI frame
    };
} // namespace Desert::Editor
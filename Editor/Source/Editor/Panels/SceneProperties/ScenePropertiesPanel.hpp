#pragma once

#include <Engine/Desert.hpp>

#include "../IPanel.hpp"

#include "ComponentEditor.hpp"

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

        // --- Thumbnails ----------------------------------------------------------------------------
        // Only a texture-id cache: component rows show the asset browser's cached PNGs. Details owns NO
        // renderer — see OnPreUpdate for why a second one broke the viewport's shadows.
        std::unique_ptr<UI::UIHelper> m_ThumbnailUI;
    };
} // namespace Desert::Editor
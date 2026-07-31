#pragma once

#include <Engine/Desert.hpp>

#include "../IPanel.hpp"

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
        void SetScene( const std::shared_ptr<Desert::Core::Scene>& scene ) override
        {
            m_Scene = scene;
        }

    private:
        void DrawMaterialEntity( const ECS::Entity& entity );

    private:
        std::shared_ptr<Desert::Core::Scene>        m_Scene;
        const std::shared_ptr<Assets::AssetManager> m_AssetManager;
        const Animation::AnimationLibrary*          m_AnimationLibrary;
        bool                                        m_DebugMode = false;
        std::string m_PrefabSavePath = Common::Constants::Path::PREFAB_PATH.string(); // post-remap
    };
} // namespace Desert::Editor
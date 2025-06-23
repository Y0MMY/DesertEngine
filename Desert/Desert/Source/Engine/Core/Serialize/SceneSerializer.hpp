#pragma once

#include <Engine/Core/Scene.hpp>
#include <glm/glm.hpp>

namespace Desert::Core
{
    class SceneSerializer
    {
    public:
        explicit SceneSerializer( const Scene* scene, const std::shared_ptr<Assets::AssetManager>& assetManager );

        std::string SerializeToJson() const;

        void SaveToFile() const;

    private:
        Scene*                                      m_Scene;
        const std::shared_ptr<Assets::AssetManager> m_AssetManager;
    };

} // namespace Desert::Core
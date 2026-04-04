#pragma once

#include <Engine/Core/Scene.hpp>
#include <glm/glm.hpp>

namespace Desert::Core
{
    class SceneSerializer
    {
    public:
        explicit SceneSerializer( const Scene* scene, const Assets::AssetManager* assetManager );

        std::string SerializeToJson() const;
        void        DeserializeFromJson( const std::string& json ) const;

        void SaveToFile() const;

    private:
        Scene*                m_Scene;
        Assets::AssetManager* m_AssetManager;
    };

} // namespace Desert::Core
#pragma once

#include <Engine/Core/Scene.hpp>
#include <glm/glm.hpp>

namespace Desert::Core
{
    class SceneSerializer
    {
    public:
        explicit SceneSerializer( const Scene* scene );

        std::string SerializeToJson() const;

        void SaveToFile() const;
    private:
        Scene* m_Scene;
    };

} // namespace Desert::Core
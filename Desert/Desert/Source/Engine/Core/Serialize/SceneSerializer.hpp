#pragma once

#include <Engine/Core/Scene.hpp>

#include <Common/Core/ResultStr.hpp>

#include <glm/glm.hpp>

#include <string_view>

namespace Desert::Core
{
    class SceneSerializer
    {
    public:
        explicit SceneSerializer( const Scene* scene, const Assets::AssetManager* assetManager );

        std::string SerializeToJson() const;

        /// Loads a scene from the JSON text of a .desce file into the scene this serializer was made for.
        ///
        /// FAILS, rather than repairs, on a file that is not at the current generation of the format
        /// (Core::kSceneVersion / Core::kUnitVersion). The error names the file, what it is, what is
        /// required and the exact SceneMigrator command that converts it — and NOTHING is created for it:
        /// not an entity, not a setting, not the scene name. The scene is left exactly as it was.
        ///
        /// @param source what to call this file in that error. A PATH when there is one; the play-mode
        ///        snapshot has no file, so it says so. It is never used to open anything - this function
        ///        does not touch the disk, and passing the path is only how the message can name it.
        [[nodiscard]] Common::BoolResultStr DeserializeFromJson( const std::string& json,
                                                                 std::string_view   source ) const;

        void SaveToFile() const;

    private:
        Scene*                m_Scene;
        Assets::AssetManager* m_AssetManager;
    };

} // namespace Desert::Core